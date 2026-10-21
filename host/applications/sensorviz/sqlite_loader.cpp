#include "sqlite_loader.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QVector3D>

#include <sqlite3.h>

#include <functional>
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
        if (stream.id == id.toStdString()) {
            return &stream;
        }
    }
    return nullptr;
}

// Replace the plot data of the streams `ids`, all on one time axis, with
// streams computed from their stored floats by `component(values, axis)`,
// and drop the uncorrected copies. Returns false, changing nothing, when a
// stream is missing or the streams do not share a time axis.
bool replaceWithCorrected(
    SensorLog &log,
    const QStringList &ids,
    const std::function<double(const float *values, int axis)> &component)
{
    QVector<SensorStream *> streams;
    std::vector<sensordata::StreamId> inputs;
    for (const QString &id : ids) {
        SensorStream *stream = streamById(log, id);
        if (!stream || stream->dataId == sensordata::kNoStream) {
            return false;
        }
        streams.append(stream);
        inputs.push_back(stream->dataId);
    }

    std::vector<sensordata::StreamId> corrected;
    for (int axis = 0; axis < streams.size(); axis++) {
        const std::optional<sensordata::StreamId> id = log.data->addComputed(
            *log.data->info(inputs[static_cast<size_t>(axis)]),
            inputs,
            [&component, axis](const float *values) { return component(values, axis); });
        if (!id) {
            for (sensordata::StreamId done : corrected) {
                log.data->remove(done);
            }
            return false;
        }
        corrected.push_back(*id);
    }
    for (int axis = 0; axis < streams.size(); axis++) {
        log.data->remove(inputs[static_cast<size_t>(axis)]);
        streams[axis]->dataId = corrected[static_cast<size_t>(axis)];
    }
    return true;
}

// The calibration matrix mixes the three magnetometer axes, so they must be
// samples of one table. The axes are corrected from the stored floats: the
// QVector3D the calibration takes is float, so this is exactly the result of
// correcting the SQLite doubles.
void applyCalibrationToImuMagnetometer(SensorLog &log)
{
    if (!log.hasCompassCalibration) {
        return;
    }
    if (!streamById(log, QStringLiteral("imu_mx")) || !streamById(log, QStringLiteral("imu_my"))
        || !streamById(log, QStringLiteral("imu_mz"))) {
        return;
    }

    const CompassCalibration calibration = log.compassCalibration;
    const bool applied = replaceWithCorrected(
        log,
        {QStringLiteral("imu_mx"), QStringLiteral("imu_my"), QStringLiteral("imu_mz")},
        [calibration](const float *values, int axis) {
            const QVector3D calibrated =
                calibration.apply(QVector3D(values[0], values[1], values[2]));
            return static_cast<double>(calibrated[axis]);
        });
    if (!applied && log.compassCalibrationWarning.isEmpty()) {
        log.compassCalibrationWarning =
            "IMUTag magnetometer calibration skipped because axis streams do not align";
    }
}

// IMUTag acceleration axes are plotted as they are stored, so the zero-g
// offset is subtracted here, once, the same way applyCalibrationToImuMagnetometer()
// corrects the magnetometer. Both are in mg. CompassTag acceleration reaches
// the plot only through CompassProcessor, which applies the offset itself.
// The offset is per axis, so each axis is corrected on its own; working from
// the stored float differs from correcting the SQLite double by the float's
// rounding, which a display transform accepts.
void applyCalibrationToImuAccelerometer(SensorLog &log)
{
    if (!log.hasCompassCalibration) {
        return;
    }
    const QVector3D offset = log.compassCalibration.accelOffset();
    const QStringList ids = {
        QStringLiteral("imu_ax"), QStringLiteral("imu_ay"), QStringLiteral("imu_az")};
    if (!streamById(log, ids[0]) || !streamById(log, ids[1]) || !streamById(log, ids[2])) {
        return;
    }
    for (int axis = 0; axis < 3; axis++) {
        replaceWithCorrected(log, {ids[axis]}, [offset, axis](const float *values, int) {
            return static_cast<double>(values[0]) - offset[axis];
        });
    }
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
    // while the plot spec decides whether it is useful enough to show.
    if (log.hasCollectionStart) {
        return;
    }

    bool have_start = false;
    double start_seconds = 0.0;
    for (const sensordata::StreamInfo &info : log.data->streams()) {
        if (info.domain != sensordata::TimeDomain::EpochSeconds) {
            continue;
        }
        if (!have_start || info.first_time < start_seconds) {
            have_start = true;
            start_seconds = info.first_time;
        }
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

    // The samples themselves are read once, by the plot backend. It validates
    // the streams metadata, reads each table in rowid order and keeps a float
    // copy for drawing; transforms that need exact values read doubles back
    // through it.
    std::string data_error;
    std::unique_ptr<sensordata::Log> data =
        sensordata::Log::open(path.toStdString(), data_error);
    if (!data) {
        error = QString::fromStdString(data_error);
        return false;
    }
    loaded.data = std::move(data);

    // Scalar rows become SensorStreams in metadata order; record-column rows
    // are grouped by group id into record sets. Empty tables load no streams.
    QMap<QString, QVector<sensordata::StreamInfo>> record_sets;
    for (const sensordata::StreamInfo &info : loaded.data->streams()) {
        if (info.kind == "record_column") {
            record_sets[QString::fromStdString(info.group_id)].append(info);
            continue;
        }
        loaded.streams.append(sensorviz::storedStreamMeta(info));
    }
    for (auto it = record_sets.cbegin(); it != record_sets.cend(); ++it) {
        const sensordata::StreamInfo &first = it.value().first();
        SensorRecordSet record_set;
        record_set.id = it.key();
        record_set.label = first.group_name.empty() ? it.key() : QString::fromStdString(first.group_name);
        record_set.table = QString::fromStdString(first.table);
        record_set.timeDomain = first.domain == sensordata::TimeDomain::ElapsedSeconds
            ? SensorTimeDomain::ElapsedSeconds
            : SensorTimeDomain::EpochSeconds;
        for (const sensordata::StreamInfo &column : it.value()) {
            record_set.columns.append(QString::fromStdString(column.value_column));
        }
        record_set.timeSource = first.handle;
        loaded.recordSets.append(record_set);
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
    applyCalibrationToImuMagnetometer(loaded);
    applyCalibrationToImuAccelerometer(loaded);

    if (loaded.streams.isEmpty() && loaded.recordSets.isEmpty()) {
        error = "No supported sensor streams or record sets found in database";
        return false;
    }

    log = loaded;
    return true;
}
