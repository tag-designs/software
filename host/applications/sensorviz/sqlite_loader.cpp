#include "sqlite_loader.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSet>
#include <QVector3D>

#include <sqlite3.h>

#include <algorithm>
#include <numeric>
#include <vector>

#include "sensor_preferences.h"

// SQLite logs are self-describing: tagcore writes a mandatory streams metadata
// table alongside the sensor tables. This file is the adapter from that on-disk
// metadata to sensorViz's normalized SensorLog model. Derived display features
// such as altitude, activity filtering, and compass orientation still live in
// sensorViz; the database describes the stored streams and record-set columns.

namespace
{

constexpr qint64 kImuHeaderMillisMask = 0x03ff;

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

SensorTimeDomain timeDomainForColumn(const QString &time_column)
{
    // IMUTag data rows use ElapsedUs; older header-derived elapsed streams use
    // StartElapsedUs. Both are microseconds from the collection start and must
    // share the elapsed axis for transforms such as temperature-aware altitude.
    return time_column.endsWith(QStringLiteral("ElapsedUs"), Qt::CaseInsensitive)
        ? SensorTimeDomain::ElapsedSeconds
        : SensorTimeDomain::EpochSeconds;
}

double normalizedTimeValue(const Statement &stmt, int column, SensorTimeDomain domain)
{
    const double value = static_cast<double>(stmt.int64Column(column));
    return domain == SensorTimeDomain::ElapsedSeconds ? value / 1000000.0 : value;
}

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
    if (tag_type.compare("BITTAG_LE", Qt::CaseInsensitive) == 0) {
        return "BitTag LE";
    }
    if (tag_type.compare("BITPRESTAG", Qt::CaseInsensitive) == 0) {
        return "BitPresTag";
    }
    if (tag_type.compare("COMPASSTAG", Qt::CaseInsensitive) == 0) {
        return "CompassTag";
    }
    if (tag_type.compare("UIUCTAG", Qt::CaseInsensitive) == 0) {
        return "UIUCTag";
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

// Columns of one table read in a single pass: one time axis and one value
// vector per requested column, all the same length and sorted by time.
struct TableColumns
{
    QVector<double> time;
    QVector<QVector<double>> values;
};

// Restore time order after a table was read in rowid order but its time column
// was found to decrease somewhere. The sort is stable, so rows with equal times
// keep their rowid order, and every value column is permuted with the time.
void sortColumnsByTime(TableColumns &columns)
{
    const qsizetype rows = columns.time.size();
    std::vector<qsizetype> order(static_cast<size_t>(rows));
    std::iota(order.begin(), order.end(), qsizetype(0));
    const QVector<double> &time = columns.time;
    std::stable_sort(order.begin(), order.end(), [&time](qsizetype a, qsizetype b) {
        return time[a] < time[b];
    });

    auto permute = [&order, rows](QVector<double> &vector) {
        QVector<double> sorted;
        sorted.reserve(rows);
        for (qsizetype index : order) {
            sorted.append(vector[index]);
        }
        vector = sorted;
    };
    permute(columns.time);
    for (QVector<double> &values : columns.values) {
        permute(values);
    }
}

// Read a table's time column and the given value columns with one query in
// rowid order. The writer inserts rows in sample order, so this is normally
// already time order and SQLite never sorts; `ElapsedUs` has no index, and an
// ORDER BY on it sorts the whole table once per query. The time order is
// checked as rows arrive, and a table that is ever out of order is sorted once
// in memory instead. The row count is read first so every vector is allocated
// once.
bool loadTableColumns(
    Database &db,
    const QString &table,
    const QString &time_column,
    const QStringList &value_columns,
    SensorTimeDomain domain,
    TableColumns &columns,
    QString &error)
{
    if (!tableExistsRaw(db, table)) {
        error = QString("Stream metadata references missing table %1").arg(table);
        return false;
    }

    qsizetype row_count = 0;
    {
        const QByteArray count_sql =
            QString("SELECT count(*) FROM %1").arg(quotedIdentifier(table)).toUtf8();
        Statement count(db, count_sql.constData());
        if (count.valid() && count.next()) {
            row_count = static_cast<qsizetype>(count.int64Column(0));
        }
    }

    QStringList select_columns;
    select_columns << quotedIdentifier(time_column);
    for (const QString &column : value_columns) {
        select_columns << quotedIdentifier(column);
    }
    const QByteArray sql = QString("SELECT %1 FROM %2 ORDER BY rowid")
                               .arg(select_columns.join(", "), quotedIdentifier(table))
                               .toUtf8();
    Statement stmt(db, sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2").arg(table, stmt.lastError());
        return false;
    }

    columns.time.clear();
    columns.time.reserve(row_count);
    columns.values = QVector<QVector<double>>(value_columns.size());
    for (QVector<double> &values : columns.values) {
        values.reserve(row_count);
    }

    bool in_order = true;
    while (stmt.next()) {
        const double time = normalizedTimeValue(stmt, 0, domain);
        if (!columns.time.isEmpty() && time < columns.time.constLast()) {
            in_order = false;
        }
        columns.time.append(time);
        for (qsizetype i = 0; i < columns.values.size(); i++) {
            columns.values[i].append(stmt.doubleColumn(static_cast<int>(i + 1)));
        }
    }

    if (!in_order) {
        sortColumnsByTime(columns);
    }
    return true;
}

// Convert every scalar stream stored in one table into SensorStream objects.
// The streams share one time column, which is read once and shared between
// them through QVector's implicit sharing, so ax, ay and az hold one copy of
// the time axis between them. Metadata is mandatory in the new schema, so a
// missing referenced table/column is a schema error rather than a normal
// optional-table condition. Streams are returned in the order of
// `definitions`; an empty table contributes none.
bool loadTableStreams(
    Database &db,
    const QVector<StreamMetadata> &definitions,
    QMap<QString, SensorStream> &streams,
    QString &error)
{
    const StreamMetadata &first = definitions.first();
    const SensorTimeDomain domain = timeDomainForColumn(first.timeColumn);
    QStringList value_columns;
    for (const StreamMetadata &definition : definitions) {
        value_columns << definition.valueColumn;
    }

    TableColumns columns;
    if (!loadTableColumns(
            db, first.table, first.timeColumn, value_columns, domain, columns, error)) {
        return false;
    }

    // Empty tables do not contribute menu items or axes.
    if (columns.time.isEmpty()) {
        return true;
    }

    for (qsizetype i = 0; i < definitions.size(); i++) {
        const StreamMetadata &definition = definitions[i];
        SensorStream stream;
        stream.id = definition.id;
        stream.label = definition.displayName;
        stream.units = definition.units;
        stream.timeDomain = domain;
        const SensorStreamDisplayDefaults defaults = defaultDisplayForStream(definition.id);
        stream.color = defaults.color;
        stream.defaultVisible = defaults.visible;
        stream.axisSide = defaults.axisSide;
        stream.axisRange = defaults.axisRange;
        stream.time = columns.time;
        stream.value = columns.values[i];
        streams.insert(definition.id, stream);
    }
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

    QStringList value_columns;
    for (const StreamMetadata &column : columns) {
        if (column.table != first.table || column.timeColumn != first.timeColumn) {
            error = QString("Record set %1 mixes tables or time columns").arg(group_id);
            return false;
        }
        value_columns << column.valueColumn;
    }

    SensorRecordSet record_set;
    record_set.id = group_id;
    record_set.label = first.groupName.isEmpty() ? group_id : first.groupName;
    record_set.timeDomain = timeDomainForColumn(first.timeColumn);

    TableColumns loaded;
    if (!loadTableColumns(
            db,
            first.table,
            first.timeColumn,
            value_columns,
            record_set.timeDomain,
            loaded,
            error)) {
        return false;
    }

    if (loaded.time.isEmpty()) {
        return true;
    }

    record_set.time = loaded.time;
    for (qsizetype i = 0; i < columns.size(); i++) {
        record_set.columns.insert(columns[i].valueColumn, loaded.values[i]);
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
    log.compassCalibration = CompassCalibration::fromCalibrationJson(root);
    log.hasCompassCalibration = true;
    log.compassCalibrationWarning.clear();
    return true;
}

SensorStream *streamById(SensorLog &log, const QString &id)
{
    for (SensorStream &stream : log.streams) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

bool streamsHaveSameSamples(
    const SensorStream &x,
    const SensorStream &y,
    const SensorStream &z)
{
    if (x.timeDomain != y.timeDomain || x.timeDomain != z.timeDomain) {
        return false;
    }
    if (x.time.size() != y.time.size() || x.time.size() != z.time.size()) {
        return false;
    }
    if (x.value.size() != y.value.size() || x.value.size() != z.value.size()) {
        return false;
    }
    for (qsizetype i = 0; i < x.time.size(); i++) {
        if (x.time[i] != y.time[i] || x.time[i] != z.time[i]) {
            return false;
        }
    }
    return true;
}

// Returns true when the magnetometer axes were changed.
bool applyCalibrationToImuMagnetometer(SensorLog &log)
{
    if (!log.hasCompassCalibration) {
        return false;
    }

    SensorStream *mx = streamById(log, QStringLiteral("imu_mx"));
    SensorStream *my = streamById(log, QStringLiteral("imu_my"));
    SensorStream *mz = streamById(log, QStringLiteral("imu_mz"));
    if (!mx || !my || !mz) {
        return false;
    }

    if (!streamsHaveSameSamples(*mx, *my, *mz)) {
        if (log.compassCalibrationWarning.isEmpty()) {
            log.compassCalibrationWarning =
                "IMUTag magnetometer calibration skipped because axis streams do not align";
        }
        return false;
    }

    for (qsizetype i = 0; i < mx->value.size(); i++) {
        const QVector3D calibrated = log.compassCalibration.apply(
            QVector3D(mx->value[i], my->value[i], mz->value[i]));
        mx->value[i] = calibrated.x();
        my->value[i] = calibrated.y();
        mz->value[i] = calibrated.z();
    }
    return true;
}

// IMUTag acceleration axes are plotted as they are stored, so the zero-g
// offset is subtracted here, once, the same way applyCalibrationToImuMagnetometer()
// corrects the magnetometer. Both are in mg. CompassTag acceleration reaches
// the plot only through CompassProcessor, which applies the offset itself.
// Returns true when the acceleration axes were changed.
bool applyCalibrationToImuAccelerometer(SensorLog &log)
{
    if (!log.hasCompassCalibration) {
        return false;
    }

    SensorStream *ax = streamById(log, QStringLiteral("imu_ax"));
    SensorStream *ay = streamById(log, QStringLiteral("imu_ay"));
    SensorStream *az = streamById(log, QStringLiteral("imu_az"));
    if (!ax || !ay || !az) {
        return false;
    }

    // The offset is per axis, so unlike the magnetometer matrix the axes need
    // not share sample times.
    const QVector3D offset = log.compassCalibration.accelOffset();
    for (double &value : ax->value) {
        value -= offset.x();
    }
    for (double &value : ay->value) {
        value -= offset.y();
    }
    for (double &value : az->value) {
        value -= offset.z();
    }
    return true;
}

// Open the decimating plot backend on the same file and point every stream
// at its data. Streams the loader changed after reading (the calibrated IMU
// axes in `corrected`) are registered as derived streams on the stored
// stream's time axis, from their corrected doubles, and the uncorrected
// stored copies are dropped so the plot never shows them.
bool attachPlotData(SensorLog &log, const QSet<QString> &corrected, QString &error)
{
    std::string data_error;
    std::unique_ptr<sensordata::Log> data =
        sensordata::Log::open(log.path.toStdString(), data_error);
    if (!data) {
        error = QString::fromStdString(data_error);
        return false;
    }

    for (SensorStream &stream : log.streams) {
        const std::optional<sensordata::StreamId> stored = data->find(stream.id.toStdString());
        if (!stored) {
            error = QString("Plot data for %1 did not load").arg(stream.id);
            return false;
        }
        if (!corrected.contains(stream.id)) {
            stream.dataId = *stored;
            continue;
        }
        const std::optional<sensordata::StreamId> derived = data->addDerived(
            *data->info(*stored), *stored, stream.value.constData(),
            static_cast<std::size_t>(stream.value.size()));
        if (!derived) {
            error = QString("Plot data for %1 could not be corrected").arg(stream.id);
            return false;
        }
        data->remove(*stored);
        stream.dataId = *derived;
    }

    log.data = std::move(data);
    return true;
}

bool loadImuCollectionStart(Database &db, SensorLog &log, QString &error)
{
    // IMUTag plots use elapsed seconds on the x-axis, but users still need an
    // absolute anchor for "when did this collection begin?" The first
    // ImuHeader row supplies that wall-clock timestamp without changing the
    // plotted time domain.
    if (!tableExistsRaw(db, "ImuHeader")) {
        return true;
    }

    Statement stmt(
        db,
        "SELECT Epoch, Millisecond FROM ImuHeader ORDER BY StartElapsedUs LIMIT 1");
    if (!stmt.valid()) {
        error = "Failed to load IMUTag collection start: " + stmt.lastError();
        return false;
    }
    if (!stmt.next()) {
        return true;
    }

    log.hasCollectionStart = true;
    log.collectionStartEpochMs =
        stmt.int64Column(0) * 1000
        + (stmt.int64Column(1) & kImuHeaderMillisMask);
    return true;
}

bool isImuDiscontinuityEvent(const QString &event)
{
    return event.startsWith(QStringLiteral("RESYNC"), Qt::CaseInsensitive)
        || event.compare(QStringLiteral("RESTART_RECOVERY"), Qt::CaseInsensitive) == 0;
}

bool loadImuEvents(Database &db, SensorLog &log, QString &error)
{
    if (!tableExistsRaw(db, "ImuEvent")) {
        return true;
    }

    Statement stmt(
        db,
        "SELECT StartElapsedUs, Event FROM ImuEvent ORDER BY StartElapsedUs");
    if (!stmt.valid()) {
        error = "Failed to load IMUTag events: " + stmt.lastError();
        return false;
    }

    SensorEventMarker resync_marker;
    resync_marker.id = QStringLiteral("imu_resync");
    resync_marker.label = QStringLiteral("IMU resync");
    resync_marker.timeDomain = SensorTimeDomain::ElapsedSeconds;

    while (stmt.next()) {
        if (isImuDiscontinuityEvent(stmt.textColumn(1))) {
            resync_marker.time.append(static_cast<double>(stmt.int64Column(0)) / 1000000.0);
        }
    }

    if (!resync_marker.time.isEmpty()) {
        log.eventMarkers.append(resync_marker);
    }

    return true;
}

void inferEpochCollectionStart(SensorLog &log)
{
    // Epoch-time logs already display date/time on the x-axis. Recording the
    // earliest epoch sample here keeps SensorLog complete for metadata panes,
    // while plotting.cpp decides whether it is useful enough to show.
    if (log.hasCollectionStart) {
        return;
    }

    bool have_start = false;
    double start_seconds = 0.0;
    auto considerTimeVector = [&](const QVector<double> &time, SensorTimeDomain domain) {
        if (domain != SensorTimeDomain::EpochSeconds || time.isEmpty()) {
            return;
        }
        const double candidate = time.first();
        if (!have_start || candidate < start_seconds) {
            have_start = true;
            start_seconds = candidate;
        }
    };

    for (const SensorStream &stream : log.streams) {
        considerTimeVector(stream.time, stream.timeDomain);
    }
    for (const SensorRecordSet &record_set : log.recordSets) {
        considerTimeVector(record_set.time, record_set.timeDomain);
    }

    if (have_start) {
        log.hasCollectionStart = true;
        log.collectionStartEpochMs = static_cast<qint64>(start_seconds * 1000.0);
    }
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

    // Scalar streams are grouped by table and time column so each table is
    // read once, then published in metadata order.
    QVector<QString> scalar_order;
    QVector<QVector<StreamMetadata>> scalar_tables;
    QMap<QString, QVector<StreamMetadata>> record_sets;
    for (const StreamMetadata &definition : stream_metadata) {
        if (definition.kind.compare("scalar", Qt::CaseInsensitive) == 0) {
            scalar_order.append(definition.id);
            auto table = std::find_if(
                scalar_tables.begin(),
                scalar_tables.end(),
                [&definition](const QVector<StreamMetadata> &group) {
                    return group.first().table == definition.table
                        && group.first().timeColumn == definition.timeColumn;
                });
            if (table == scalar_tables.end()) {
                scalar_tables.append(QVector<StreamMetadata>{definition});
            } else {
                table->append(definition);
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

    QMap<QString, SensorStream> scalar_streams;
    for (const QVector<StreamMetadata> &table : scalar_tables) {
        if (!loadTableStreams(db, table, scalar_streams, error)) {
            return false;
        }
    }
    for (const QString &id : scalar_order) {
        auto stream = scalar_streams.find(id);
        if (stream != scalar_streams.end()) {
            loaded.streams.append(stream.value());
            scalar_streams.erase(stream);
        }
    }

    for (auto it = record_sets.cbegin(); it != record_sets.cend(); ++it) {
        if (!loadRecordSet(db, it.key(), it.value(), loaded, error)) {
            return false;
        }
    }

    for (const SensorStream &stream : loaded.streams) {
        if (stream.timeDomain == SensorTimeDomain::ElapsedSeconds) {
            loaded.timeDomain = SensorTimeDomain::ElapsedSeconds;
            break;
        }
    }
    for (const SensorRecordSet &record_set : loaded.recordSets) {
        if (record_set.timeDomain == SensorTimeDomain::ElapsedSeconds) {
            loaded.timeDomain = SensorTimeDomain::ElapsedSeconds;
            break;
        }
    }
    if (!loadImuCollectionStart(db, loaded, error)) {
        return false;
    }
    if (!loadImuEvents(db, loaded, error)) {
        return false;
    }
    inferEpochCollectionStart(loaded);
    if ((tableExistsRaw(db, "Compass") || tableExistsRaw(db, "Calibration"))
        && !loadCompassCalibration(db, loaded, error)) {
        return false;
    }
    QSet<QString> corrected;
    if (applyCalibrationToImuMagnetometer(loaded)) {
        corrected << QStringLiteral("imu_mx") << QStringLiteral("imu_my") << QStringLiteral("imu_mz");
    }
    if (applyCalibrationToImuAccelerometer(loaded)) {
        corrected << QStringLiteral("imu_ax") << QStringLiteral("imu_ay") << QStringLiteral("imu_az");
    }

    if (loaded.streams.isEmpty() && loaded.recordSets.isEmpty()) {
        error = "No supported sensor streams or record sets found in database";
        return false;
    }

    if (!attachPlotData(loaded, corrected, error)) {
        return false;
    }

    log = loaded;
    return true;
}
