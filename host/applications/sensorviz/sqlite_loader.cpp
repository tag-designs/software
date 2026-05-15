#include "sqlite_loader.h"

#include "sensorprofile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <sqlite3.h>

// SQLite logs from different tag types share table conventions, but no single
// tag is expected to write every table. This file is the adapter from that
// sparse on-disk schema to sensorViz's normalized SensorLog model.
//
// To add support for a new scalar table:
// - confirm the table has an Epoch column and one numeric value column
// - add one ScalarStreamDefinition in sensorprofile.cpp
// - choose a stable stream id; controls.cpp uses ids for transforms and ranges
//
// To add support for a multi-column table, add a RecordSetDefinition. Later
// transform code can consume that record set and produce plottable streams.

namespace
{

// Small RAII wrappers keep the SQLite C API localized to this file. sensorViz
// only needs read-only queries, so these wrappers intentionally stay minimal.
class Database
{
public:
    explicit Database(const QString &path)
    {
        const QByteArray utf8_path = path.toUtf8();
        if (sqlite3_open_v2(utf8_path.constData(), &db_, SQLITE_OPEN_READONLY, nullptr)
            != SQLITE_OK) {
            last_error_ = errorString();
            close();
        }
    }

    ~Database()
    {
        close();
    }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool isOpen() const
    {
        return db_ != nullptr;
    }

    sqlite3 *get() const
    {
        return db_;
    }

    QString lastError() const
    {
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    void close()
    {
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
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            last_error_ = errorString();
            stmt_ = nullptr;
        }
    }

    ~Statement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    bool valid() const
    {
        return stmt_ != nullptr;
    }

    bool next()
    {
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
        return sqlite3_column_int64(stmt_, column);
    }

    double doubleColumn(int column) const
    {
        return sqlite3_column_double(stmt_, column);
    }

    QString textColumn(int column) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return QString::fromUtf8(reinterpret_cast<const char *>(text ? text : (const unsigned char *)""));
    }

    QString lastError() const
    {
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    QString last_error_;
};

bool tableExistsRaw(Database &db, const QString &table_name)
{
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

// Convert one optional SQLite table into a SensorStream. A missing table is not
// an error because each tag type writes a different subset of sensor tables.
// A present-but-invalid table is an error because that usually means the log
// was produced with a schema this viewer does not understand yet.
bool loadStream(
    Database &db,
    const ScalarStreamDefinition &definition,
    SensorLog &log,
    QString &error)
{
    if (!tableExistsRaw(db, definition.table)) {
        return true;
    }

    const QString sql = QString("SELECT Epoch, %1 FROM %2 ORDER BY Epoch")
                            .arg(definition.valueColumn, definition.table);
    const QByteArray utf8_sql = sql.toUtf8();
    Statement stmt(db, utf8_sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2").arg(definition.label, stmt.lastError());
        return false;
    }

    SensorStream stream;
    stream.id = definition.id;
    stream.label = definition.label;
    stream.units = definition.units;
    stream.color = definition.color;
    stream.defaultVisible = definition.defaultVisible;
    stream.axisSide = definition.axisSide;
    stream.axisRange = definition.axisRange;

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
    const RecordSetDefinition &definition,
    SensorLog &log,
    QString &error)
{
    if (!tableExistsRaw(db, definition.table)) {
        return true;
    }

    QStringList select_columns;
    select_columns << "Epoch";
    select_columns << definition.valueColumns;
    const QString sql = QString("SELECT %1 FROM %2 ORDER BY Epoch")
                            .arg(select_columns.join(", "), definition.table);
    const QByteArray utf8_sql = sql.toUtf8();
    Statement stmt(db, utf8_sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2").arg(definition.label, stmt.lastError());
        return false;
    }

    SensorRecordSet record_set;
    record_set.id = definition.id;
    record_set.label = definition.label;
    for (const QString &column : definition.valueColumns) {
        record_set.columns.insert(column, QVector<double>());
    }

    while (stmt.next()) {
        record_set.time.append(stmt.int64Column(0));
        for (int i = 0; i < definition.valueColumns.size(); i++) {
            record_set.columns[definition.valueColumns[i]].append(stmt.doubleColumn(i + 1));
        }
    }

    if (record_set.time.isEmpty()) {
        return true;
    }

    log.recordSets.append(record_set);
    return true;
}

bool loadCompassCalibration(Database &db, SensorLog &log, QString &error)
{
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

bool SqliteLoader::load(const QString &path, SensorLog &log, QString &error)
{
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

    // The profile is selected after reading metadata, but table availability is
    // still the source of truth. Definitions for missing tables are ignored.
    const SensorProfile profile = sensorProfileForTag(loaded.tagType);
    loaded.profileName = profile.displayName;
    for (const ScalarStreamDefinition &definition : profile.scalarStreams) {
        if (!loadStream(db, definition, loaded, error)) {
            return false;
        }
    }
    for (const RecordSetDefinition &definition : profile.recordSets) {
        if (!loadRecordSet(db, definition, loaded, error)) {
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
