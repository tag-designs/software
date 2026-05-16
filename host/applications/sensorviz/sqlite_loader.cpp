#include "sqlite_loader.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>

#include <sqlite3.h>

#include "sensor_preferences.h"

// SQLite logs are self-describing: tagcore writes a mandatory streams metadata
// table alongside the sensor tables. This file is the adapter from that on-disk
// metadata to sensorViz's normalized SensorLog model. Derived display features
// such as altitude, activity filtering, and compass orientation still live in
// sensorViz; the database describes the stored streams and record-set columns.

namespace
{

// ---------------------------------------------------------------------------
// SQLite RAII Wrappers
// ---------------------------------------------------------------------------
//
// These classes are intentionally tiny and private to this file. They hide the
// sqlite3 C API from the rest of sensorViz while preserving enough error detail
// for the File -> Load failure dialog.

// Small RAII wrappers keep the SQLite C API localized to this file. sensorViz
// only needs read-only queries, so these wrappers intentionally stay minimal.
class Database
{
public:
    explicit Database(const QString &path)
    {
        // Open read-only so visualizing a log can never mutate it.
        const QByteArray utf8_path = path.toUtf8();
        if (sqlite3_open_v2(utf8_path.constData(), &db_, SQLITE_OPEN_READONLY, nullptr)
            != SQLITE_OK) {
            last_error_ = errorString();
            close();
        }
    }

    ~Database()
    {
        // Always close the sqlite handle even on early-return error paths.
        close();
    }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool isOpen() const
    {
        // Used by the public loader to report open failures before preparing
        // statements.
        return db_ != nullptr;
    }

    sqlite3 *get() const
    {
        // Exposes the raw handle only inside this adapter file.
        return db_;
    }

    QString lastError() const
    {
        // Prefer the error captured at failure time; sqlite3_errmsg can change
        // after later API calls.
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        // Normalize sqlite's C string error into QString.
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    void close()
    {
        // Idempotent close helper used by the destructor and failed open path.
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    QString last_error_;
};

class Statement
{
public:
    Statement(Database &db, const char *sql) : db_(db.get())
    {
        // Prepare immediately so valid() can be checked before stepping.
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            last_error_ = errorString();
            stmt_ = nullptr;
        }
    }

    ~Statement()
    {
        // Finalize prepared statements on all paths.
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    bool valid() const
    {
        // A failed prepare leaves stmt_ null and last_error_ populated.
        return stmt_ != nullptr;
    }

    bool next()
    {
        // Advance to the next row. Non-DONE failures are retained for callers
        // that need an error message.
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            last_error_ = errorString();
        }
        return false;
    }

    qint64 int64Column(int column) const
    {
        // Epoch columns are read through this helper.
        return sqlite3_column_int64(stmt_, column);
    }

    double doubleColumn(int column) const
    {
        // Sensor values are read as doubles even when sqlite stores integers.
        return sqlite3_column_double(stmt_, column);
    }

    int intColumn(int column) const
    {
        return sqlite3_column_int(stmt_, column);
    }

    bool isNull(int column) const
    {
        return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
    }

    QString textColumn(int column) const
    {
        // Info values and JSON blobs are UTF-8 text in current logs.
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return QString::fromUtf8(reinterpret_cast<const char *>(text ? text : (const unsigned char *)""));
    }

    QString lastError() const
    {
        // Return the prepare/step error captured closest to the failing call.
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        // Convert sqlite statement errors into QString for UI messages.
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    QString last_error_;
};

// ---------------------------------------------------------------------------
// Loader Metadata Types
// ---------------------------------------------------------------------------
//
// StreamMetadata is the in-memory copy of one row from tagcore's streams table.
// Rows with kind="scalar" become SensorStream objects. Rows with
// kind="record_column" are grouped by groupId and become SensorRecordSet
// objects for transform code such as CompassTag orientation derivation.

struct StreamMetadata
{
    QString id;
    QString groupId;
    QString groupName;
    QString table;
    QString timeColumn;
    QString valueColumn;
    QString kind;
    QString displayName;
    QString units;
};

// ---------------------------------------------------------------------------
// Generic Helpers
// ---------------------------------------------------------------------------
//
// Helpers in this section normalize SQLite/schema vocabulary into sensorViz
// model values. They do not query sensor data rows.

QString displayNameForTag(const QString &tag_type)
{
    if (tag_type.compare("PRESTAG", Qt::CaseInsensitive) == 0) {
        return "PresTag";
    }
    if (tag_type.compare("BITTAG", Qt::CaseInsensitive) == 0) {
        return "BitTag";
    }
    if (tag_type.compare("COMPASSTAG", Qt::CaseInsensitive) == 0) {
        return "CompassTag";
    }
    if (!tag_type.isEmpty()) {
        return tag_type;
    }
    return "Sensor log";
}

QString quotedIdentifier(const QString &identifier)
{
    QString escaped = identifier;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

bool tableExistsRaw(Database &db, const QString &table_name)
{
    // Case-insensitive table test used before loading metadata-described data.
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND lower(name)=lower(?)";
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray utf8_name = table_name.toUtf8();
    sqlite3_bind_text(stmt, 1, utf8_name.constData(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

// ---------------------------------------------------------------------------
// Metadata Loading
// ---------------------------------------------------------------------------
//
// The streams table is mandatory for new SQLite logs. Loading it first gives
// sensorViz the table names, column names, labels, and units needed to read the
// actual sensor samples.

bool loadStreamMetadata(Database &db, QVector<StreamMetadata> &metadata, QString &error)
{
    if (!tableExistsRaw(db, "streams")) {
        error = "SQLite log does not contain required stream metadata";
        return false;
    }

    Statement stmt(
        db,
        "SELECT stream_id, group_id, group_name, table_name, time_column, "
        "value_column, stream_kind, display_name, units "
        "FROM streams ORDER BY rowid");
    if (!stmt.valid()) {
        error = "Failed to load stream metadata: " + stmt.lastError();
        return false;
    }

    while (stmt.next()) {
        StreamMetadata stream;
        stream.id = stmt.textColumn(0);
        stream.groupId = stmt.textColumn(1);
        stream.groupName = stmt.textColumn(2);
        stream.table = stmt.textColumn(3);
        stream.timeColumn = stmt.textColumn(4);
        stream.valueColumn = stmt.textColumn(5);
        stream.kind = stmt.textColumn(6);
        stream.displayName = stmt.textColumn(7);
        stream.units = stmt.textColumn(8);

        if (stream.id.isEmpty()
            || stream.table.isEmpty()
            || stream.timeColumn.isEmpty()
            || stream.valueColumn.isEmpty()
            || stream.kind.isEmpty()
            || stream.displayName.isEmpty()) {
            error = "Stream metadata contains an incomplete row";
            return false;
        }

        metadata.append(stream);
    }

    if (metadata.isEmpty()) {
        error = "SQLite log contains no stream metadata rows";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Sensor Data Loading
// ---------------------------------------------------------------------------
//
// These functions use StreamMetadata to query the referenced tables. Scalar
// rows become plottable streams. Record-column rows are loaded together as one
// multi-column record set for later transform code.

// Convert one metadata-described SQLite column into a SensorStream. Metadata is
// mandatory in the new schema, so a missing referenced table/column is a schema
// error rather than a normal optional-table condition.
bool loadStream(
    Database &db,
    const StreamMetadata &definition,
    SensorLog &log,
    QString &error)
{
    if (!tableExistsRaw(db, definition.table)) {
        error = QString("Stream metadata references missing table %1").arg(definition.table);
        return false;
    }

    const QString sql = QString("SELECT %1, %2 FROM %3 ORDER BY %1")
                            .arg(
                                quotedIdentifier(definition.timeColumn),
                                quotedIdentifier(definition.valueColumn),
                                quotedIdentifier(definition.table));
    const QByteArray utf8_sql = sql.toUtf8();
    Statement stmt(db, utf8_sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2").arg(definition.displayName, stmt.lastError());
        return false;
    }

    SensorStream stream;
    stream.id = definition.id;
    stream.label = definition.displayName;
    stream.units = definition.units;
    const SensorStreamDisplayDefaults defaults = defaultDisplayForStream(definition.id);
    stream.color = defaults.color;
    stream.defaultVisible = defaults.visible;
    stream.axisSide = defaults.axisSide;
    stream.axisRange = defaults.axisRange;

    while (stmt.next()) {
        stream.time.append(stmt.int64Column(0));
        stream.value.append(stmt.doubleColumn(1));
    }

    // Empty tables do not contribute menu items or axes.
    if (stream.time.isEmpty()) {
        return true;
    }

    log.streams.append(stream);
    return true;
}

// Multi-column record sets are loaded but not plotted directly. They are the
// extension point for compass-style data, where one table can later produce
// several computed streams and/or a specialized visualization panel.
bool loadRecordSet(
    Database &db,
    const QString &group_id,
    const QVector<StreamMetadata> &columns,
    SensorLog &log,
    QString &error)
{
    if (columns.isEmpty()) {
        return true;
    }

    const StreamMetadata first = columns.first();
    if (!tableExistsRaw(db, first.table)) {
        error = QString("Stream metadata references missing table %1").arg(first.table);
        return false;
    }

    QStringList select_columns;
    select_columns << quotedIdentifier(first.timeColumn);
    for (const StreamMetadata &column : columns) {
        if (column.table != first.table || column.timeColumn != first.timeColumn) {
            error = QString("Record set %1 mixes tables or time columns").arg(group_id);
            return false;
        }
        select_columns << quotedIdentifier(column.valueColumn);
    }

    const QString sql = QString("SELECT %1 FROM %2 ORDER BY %3")
                            .arg(
                                select_columns.join(", "),
                                quotedIdentifier(first.table),
                                quotedIdentifier(first.timeColumn));
    const QByteArray utf8_sql = sql.toUtf8();
    Statement stmt(db, utf8_sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2")
                    .arg(first.groupName.isEmpty() ? group_id : first.groupName, stmt.lastError());
        return false;
    }

    SensorRecordSet record_set;
    record_set.id = group_id;
    record_set.label = first.groupName.isEmpty() ? group_id : first.groupName;
    for (const StreamMetadata &column : columns) {
        record_set.columns.insert(column.valueColumn, QVector<double>());
    }

    while (stmt.next()) {
        record_set.time.append(stmt.int64Column(0));
        for (int i = 0; i < columns.size(); i++) {
            record_set.columns[columns[i].valueColumn].append(stmt.doubleColumn(i + 1));
        }
    }

    if (record_set.time.isEmpty()) {
        return true;
    }

    log.recordSets.append(record_set);
    return true;
}

// ---------------------------------------------------------------------------
// Specialized Metadata Loading
// ---------------------------------------------------------------------------
//
// Calibration constants are not ordinary plottable streams, but CompassTag
// derived streams need them. Keep this parsing isolated so the public loader can
// assemble SensorLog without exposing SQLite details to transform code.

bool loadCompassCalibration(Database &db, SensorLog &log, QString &error)
{
    // CompassTag calibration is stored as JSON in the latest Calibration row.
    // Load it as typed metadata so compass_transforms.cpp can derive streams
    // without re-querying or reparsing.
    if (!tableExistsRaw(db, "Calibration")) {
        return true;
    }

    Statement stmt(db, "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");
    if (!stmt.valid()) {
        error = "Failed to load compass calibration: " + stmt.lastError();
        return false;
    }

    if (!stmt.next()) {
        log.compassCalibrationWarning = "Calibration table is present but empty";
        return true;
    }

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(stmt.textColumn(1).toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        log.compassCalibrationWarning =
            QString("Compass calibration constants are not valid JSON: %1")
                .arg(parse_error.errorString());
        return true;
    }

    const QJsonObject root = document.object();
    const QJsonObject magnetometer = root.value("magnetometer").toObject();
    if (magnetometer.isEmpty()) {
        log.compassCalibrationWarning =
            "Compass calibration constants do not contain a magnetometer object";
        return true;
    }

    log.compassCalibrationEpoch = stmt.int64Column(0);
    log.compassCalibration = CompassCalibration::fromMagnetometerJson(magnetometer);
    log.hasCompassCalibration = true;
    log.compassCalibrationWarning.clear();
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public Loader
// ---------------------------------------------------------------------------
//
// This is the only function called by the rest of sensorViz. It orchestrates
// opening the database, copying file metadata, loading stream metadata, loading
// data tables, and publishing a complete SensorLog only after all required
// pieces have succeeded.

bool SqliteLoader::load(const QString &path, SensorLog &log, QString &error)
{
    // Public load orchestration: open the database, read metadata, load the
    // self-described streams/record sets, then atomically replace the caller's
    // SensorLog on success.
    Database db(path);
    if (!db.isOpen()) {
        error = "Could not open database: " + db.lastError();
        return false;
    }

    SensorLog loaded;
    loaded.path = path;

    // Preserve the info table for the File Info tab and use tagtype only as
    // metadata. Feature availability is inferred from actual tables/streams.
    Statement info_query(db, "SELECT fieldname, value FROM info");
    if (info_query.valid()) {
        while (info_query.next()) {
            const QString field = info_query.textColumn(0);
            const QString value = info_query.textColumn(1);
            loaded.info.insert(field, value);
            if (field.compare("tagtype", Qt::CaseInsensitive) == 0) {
                loaded.tagType = value;
            }
        }
    }

    loaded.profileName = displayNameForTag(loaded.tagType);

    QVector<StreamMetadata> stream_metadata;
    if (!loadStreamMetadata(db, stream_metadata, error)) {
        return false;
    }

    QMap<QString, QVector<StreamMetadata>> record_sets;
    for (const StreamMetadata &definition : stream_metadata) {
        if (definition.kind.compare("scalar", Qt::CaseInsensitive) == 0) {
            if (!loadStream(db, definition, loaded, error)) {
                return false;
            }
        } else if (definition.kind.compare("record_column", Qt::CaseInsensitive) == 0) {
            if (definition.groupId.isEmpty()) {
                error = QString("Record column %1 does not specify a group").arg(definition.id);
                return false;
            }
            record_sets[definition.groupId].append(definition);
        } else {
            error = QString("Unsupported stream kind %1 for stream %2")
                        .arg(definition.kind, definition.id);
            return false;
        }
    }

    for (auto it = record_sets.cbegin(); it != record_sets.cend(); ++it) {
        if (!loadRecordSet(db, it.key(), it.value(), loaded, error)) {
            return false;
        }
    }
    if ((tableExistsRaw(db, "Compass") || tableExistsRaw(db, "Calibration"))
        && !loadCompassCalibration(db, loaded, error)) {
        return false;
    }

    if (loaded.streams.isEmpty() && loaded.recordSets.isEmpty()) {
        error = "No supported sensor streams or record sets found in database";
        return false;
    }

    log = loaded;
    return true;
}
