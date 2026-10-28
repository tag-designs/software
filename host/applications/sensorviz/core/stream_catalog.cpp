/**
 * @file    stream_catalog.cpp
 * @brief   openLog(): reads a tag SQLite log's metadata into a StreamCatalog.
 *
 * @details SQLite logs are self-describing: tagcore writes a mandatory
 *          `streams` metadata table beside the sensor tables, and
 *          sensordata reads the samples it describes. This file reads the
 *          rest -- `info`, the IMUTag header and events, the compass
 *          calibration -- with read-only queries. Derived display streams
 *          (altitude, magnitudes, compass orientation) are the transforms'
 *          business, not the catalog's.
 *
 *          It replaced the Qt loader (`sqlite_loader.cpp`) and produces the
 *          same catalog for every log: comparisons that were case-insensitive
 *          there are ASCII case-insensitive here, which agrees on every name
 *          tagcore writes.
 */
#include "stream_catalog.h"

#include <sqlite3.h>

#include <cctype>
#include <functional>
#include <optional>
#include <utility>

#include "calibration_json.h"

namespace sensorviz {
namespace {

/// IMUTag's ImuHeader.Millisecond carries the millisecond in its low 10 bits.
constexpr std::int64_t kImuHeaderMillisMask = 0x03ff;

/**
 * @class   Database
 * @brief   A read-only SQLite connection, closed on destruction.
 */
class Database
{
public:
    /// Opens @p path read-only, so viewing a log can never change it.
    explicit Database(const std::string &path)
    {
        if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            error_ = message();
            close();
        }
    }
    ~Database() { close(); }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// False when the open failed; lastError() says why.
    bool isOpen() const { return db_ != nullptr; }
    /// The raw handle, for Statement.
    sqlite3 *get() const { return db_; }
    /// The error captured at failure, else SQLite's current message.
    std::string lastError() const { return error_.empty() ? message() : error_; }

private:
    /// SQLite's message for the handle, or a fixed one when there is none.
    std::string message() const { return db_ ? sqlite3_errmsg(db_) : "database is not open"; }
    /// Closes the handle; safe to call twice.
    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr; ///< The connection; null when not open.
    std::string error_;     ///< The open failure's message.
};

/**
 * @class   Statement
 * @brief   A prepared statement, finalized on destruction.
 */
class Statement
{
public:
    /// Prepares @p sql on @p db; valid() reports whether that worked.
    Statement(Database &db, const char *sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            error_ = message();
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

    /// False when the prepare failed.
    bool valid() const { return stmt_ != nullptr; }
    /// Binds text to the 1-based parameter @p index.
    void bind(int index, const std::string &text)
    {
        sqlite3_bind_text(stmt_, index, text.c_str(), -1, SQLITE_TRANSIENT);
    }
    /// Steps to the next row: true for a row, false when done or on error.
    bool next()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            error_ = message();
        }
        return false;
    }
    /// Column @p column of the current row as a 64-bit integer.
    std::int64_t int64Column(int column) const { return sqlite3_column_int64(stmt_, column); }
    /// Column @p column of the current row as UTF-8 text; empty for NULL.
    std::string textColumn(int column) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return text ? reinterpret_cast<const char *>(text) : "";
    }
    /// The error captured closest to the failing call.
    std::string lastError() const { return error_.empty() ? message() : error_; }

private:
    /// SQLite's message for the connection.
    std::string message() const { return db_ ? sqlite3_errmsg(db_) : "database is not open"; }

    sqlite3 *db_ = nullptr;          ///< The connection the statement belongs to.
    sqlite3_stmt *stmt_ = nullptr;   ///< The statement; null when the prepare failed.
    std::string error_;              ///< The prepare or step failure's message.
};

/// ASCII lower case of @p text.
std::string lower(std::string text)
{
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

/// True when @p a and @p b are equal ignoring ASCII case.
bool equalsIgnoringCase(const std::string &a, const char *b)
{
    return lower(a) == lower(b);
}

/// The display name for a tag type, matched without regard to case.
std::string displayNameForTag(const std::string &tagType)
{
    static const std::pair<const char *, const char *> names[] = {
        {"PRESTAG", "PresTag"},
        {"BITTAG", "BitTag"},
        {"BITTAG_LE", "BitTag LE"},
        {"BITPRESTAG", "BitPresTag"},
        {"COMPASSTAG", "CompassTag"},
        {"UIUCTAG", "UIUCTag"},
    };
    for (const auto &name : names) {
        if (equalsIgnoringCase(tagType, name.first)) {
            return name.second;
        }
    }
    return tagType.empty() ? "Sensor log" : tagType;
}

/// True when a table named @p name exists, matched without regard to case.
bool tableExists(Database &db, const std::string &name)
{
    Statement stmt(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND lower(name)=lower(?)");
    if (!stmt.valid()) {
        return false;
    }
    stmt.bind(1, name);
    return stmt.next();
}

/**
 * @brief   Reads the newest Calibration row into @p catalog.
 * @return  false, with @p error set, only when the query cannot be prepared;
 *          an empty table or unusable JSON leaves a warning instead.
 */
bool loadCompassCalibration(Database &db, StreamCatalog &catalog, std::string &error)
{
    if (!tableExists(db, "Calibration")) {
        return true;
    }
    Statement stmt(db, "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");
    if (!stmt.valid()) {
        error = "Failed to load compass calibration: " + stmt.lastError();
        return false;
    }
    if (!stmt.next()) {
        catalog.compassCalibrationWarning = "Calibration table is present but empty";
        return true;
    }
    compasscore::CalibrationConstants constants;
    std::string reason;
    if (!compasscore::calibrationFromJson(stmt.textColumn(1), constants, reason)) {
        // The reader's reasons begin "calibration constants ...".
        catalog.compassCalibrationWarning = "Compass " + reason;
        return true;
    }
    catalog.compassCalibrationEpoch = stmt.int64Column(0);
    catalog.compassCalibration = constants;
    catalog.hasCompassCalibration = true;
    catalog.compassCalibrationWarning.clear();
    return true;
}

/// The stored stream @p id, or null.
StreamMeta *streamById(StreamCatalog &catalog, const char *id)
{
    for (StreamMeta &stream : catalog.streams) {
        if (stream.id == id) {
            return &stream;
        }
    }
    return nullptr;
}

/**
 * @brief   Replaces streams on one time axis with corrected copies.
 *
 * @details Each stream in @p ids becomes a computed stream whose value is
 *          `component(values, axis)`, where `values` are the stored floats
 *          of all of @p ids at that sample and `axis` is the stream's place
 *          in @p ids; the uncorrected streams are removed from the Log.
 *
 * @return  false, changing nothing, when a stream is missing or the streams
 *          do not share a time axis.
 */
bool replaceWithCorrected(
    StreamCatalog &catalog,
    const std::vector<const char *> &ids,
    const std::function<double(const float *values, int axis)> &component)
{
    std::vector<StreamMeta *> streams;
    std::vector<sensordata::StreamId> inputs;
    for (const char *id : ids) {
        StreamMeta *stream = streamById(catalog, id);
        if (!stream || stream->dataId == sensordata::kNoStream) {
            return false;
        }
        streams.push_back(stream);
        inputs.push_back(stream->dataId);
    }

    std::vector<sensordata::StreamId> corrected;
    for (size_t axis = 0; axis < streams.size(); axis++) {
        const std::optional<sensordata::StreamId> id = catalog.data->addComputed(
            *catalog.data->info(inputs[axis]),
            inputs,
            [&component, axis](const float *values) { return component(values, static_cast<int>(axis)); });
        if (!id) {
            for (sensordata::StreamId done : corrected) {
                catalog.data->remove(done);
            }
            return false;
        }
        corrected.push_back(*id);
    }
    for (size_t axis = 0; axis < streams.size(); axis++) {
        catalog.data->remove(inputs[axis]);
        streams[axis]->dataId = corrected[axis];
    }
    return true;
}

/**
 * @brief   Calibrates IMUTag's magnetometer axes in place.
 *
 * @details The soft-iron matrix mixes the three axes, so they must be samples
 *          of one table. They are corrected from the stored floats, which is
 *          exactly correcting the SQLite doubles, since the calibration takes
 *          float vectors.
 */
void applyCalibrationToImuMagnetometer(StreamCatalog &catalog)
{
    if (!catalog.hasCompassCalibration || !streamById(catalog, "imu_mx")
        || !streamById(catalog, "imu_my") || !streamById(catalog, "imu_mz")) {
        return;
    }
    const compasscore::CalibrationConstants calibration = catalog.compassCalibration;
    const bool applied = replaceWithCorrected(
        catalog, {"imu_mx", "imu_my", "imu_mz"}, [calibration](const float *values, int axis) {
            const sensormath::Vec3f calibrated = compasscore::applyMagnetometer(
                calibration, sensormath::Vec3f{values[0], values[1], values[2]});
            return static_cast<double>(axis == 0 ? calibrated.x : axis == 1 ? calibrated.y : calibrated.z);
        });
    if (!applied && catalog.compassCalibrationWarning.empty()) {
        catalog.compassCalibrationWarning =
            "IMUTag magnetometer calibration skipped because axis streams do not align";
    }
}

/**
 * @brief   Subtracts the accelerometer zero-g offset from IMUTag's axes.
 *
 * @details IMUTag acceleration is plotted as stored, so the offset (mg, like
 *          the samples) is removed here, once. CompassTag acceleration reaches
 *          the plot only through the compass transform, which applies the
 *          offset itself. Working from the stored float differs from
 *          correcting the SQLite double by the float's rounding, which a
 *          display accepts.
 */
void applyCalibrationToImuAccelerometer(StreamCatalog &catalog)
{
    if (!catalog.hasCompassCalibration) {
        return;
    }
    const sensormath::Vec3f offset = catalog.compassCalibration.accelOffset;
    const char *ids[] = {"imu_ax", "imu_ay", "imu_az"};
    if (!streamById(catalog, ids[0]) || !streamById(catalog, ids[1]) || !streamById(catalog, ids[2])) {
        return;
    }
    for (int axis = 0; axis < 3; axis++) {
        const float component = axis == 0 ? offset.x : axis == 1 ? offset.y : offset.z;
        replaceWithCorrected(catalog, {ids[axis]}, [component](const float *values, int) {
            return static_cast<double>(values[0]) - component;
        });
    }
}

/**
 * @brief   Reads IMUTag's wall-clock start of collection from ImuHeader.
 *
 * @details IMUTag plots elapsed seconds, but File Info and the plot's
 *          metadata box still want an absolute anchor; the first header row
 *          gives it without changing the plotted time domain.
 */
bool loadImuCollectionStart(Database &db, StreamCatalog &catalog, std::string &error)
{
    if (!tableExists(db, "ImuHeader")) {
        return true;
    }
    Statement stmt(db, "SELECT Epoch, Millisecond FROM ImuHeader ORDER BY StartElapsedUs LIMIT 1");
    if (!stmt.valid()) {
        error = "Failed to load IMUTag collection start: " + stmt.lastError();
        return false;
    }
    if (!stmt.next()) {
        return true;
    }
    catalog.hasCollectionStart = true;
    catalog.collectionStartEpochMs =
        stmt.int64Column(0) * 1000 + (stmt.int64Column(1) & kImuHeaderMillisMask);
    return true;
}

/// True for the IMUTag events that break the time base: RESYNC* and RESTART_RECOVERY.
bool isImuDiscontinuityEvent(const std::string &event)
{
    return lower(event.substr(0, 6)) == "resync" || equalsIgnoringCase(event, "RESTART_RECOVERY");
}

/// Collects IMUTag's discontinuity events into the `imu_resync` marker.
bool loadImuEvents(Database &db, StreamCatalog &catalog, std::string &error)
{
    if (!tableExists(db, "ImuEvent")) {
        return true;
    }
    Statement stmt(db, "SELECT StartElapsedUs, Event FROM ImuEvent ORDER BY StartElapsedUs");
    if (!stmt.valid()) {
        error = "Failed to load IMUTag events: " + stmt.lastError();
        return false;
    }
    EventMarker resync;
    resync.id = "imu_resync";
    resync.label = "IMU resync";
    resync.timeDomain = sensordata::TimeDomain::ElapsedSeconds;
    while (stmt.next()) {
        if (isImuDiscontinuityEvent(stmt.textColumn(1))) {
            resync.time.push_back(static_cast<double>(stmt.int64Column(0)) / 1000000.0);
        }
    }
    if (!resync.time.empty()) {
        catalog.eventMarkers.push_back(std::move(resync));
    }
    return true;
}

/// For epoch-time logs, takes the earliest stream's first sample as the start.
void inferEpochCollectionStart(StreamCatalog &catalog)
{
    if (catalog.hasCollectionStart) {
        return;
    }
    bool haveStart = false;
    double startSeconds = 0.0;
    for (const sensordata::StreamInfo &info : catalog.data->streams()) {
        if (info.domain != sensordata::TimeDomain::EpochSeconds) {
            continue;
        }
        if (!haveStart || info.first_time < startSeconds) {
            haveStart = true;
            startSeconds = info.first_time;
        }
    }
    if (haveStart) {
        catalog.hasCollectionStart = true;
        catalog.collectionStartEpochMs = static_cast<std::int64_t>(startSeconds * 1000.0);
    }
}

} // namespace

bool openLog(const std::string &path, StreamCatalog &catalog, std::string &error)
{
    Database db(path);
    if (!db.isOpen()) {
        error = "Could not open database: " + db.lastError();
        return false;
    }

    StreamCatalog loaded;
    loaded.path = path;

    Statement infoQuery(db, "SELECT fieldname, value FROM info");
    if (infoQuery.valid()) {
        while (infoQuery.next()) {
            const std::string field = infoQuery.textColumn(0);
            const std::string value = infoQuery.textColumn(1);
            loaded.info[field] = value;
            if (equalsIgnoringCase(field, "tagtype")) {
                loaded.tagType = value;
            }
        }
    }
    loaded.profileName = displayNameForTag(loaded.tagType);

    std::string dataError;
    std::unique_ptr<sensordata::Log> data = sensordata::Log::open(path, dataError);
    if (!data) {
        error = dataError;
        return false;
    }
    loaded.data = std::move(data);

    // Scalar rows become streams in metadata order; record-column rows are
    // grouped by group id into record sets. Empty tables load no streams.
    std::map<std::string, std::vector<sensordata::StreamInfo>> recordColumns;
    for (const sensordata::StreamInfo &info : loaded.data->streams()) {
        if (info.kind == "record_column") {
            recordColumns[info.group_id].push_back(info);
            continue;
        }
        loaded.streams.push_back(storedStreamMeta(info));
    }
    for (const auto &[id, columns] : recordColumns) {
        const sensordata::StreamInfo &first = columns.front();
        RecordSet recordSet;
        recordSet.id = id;
        recordSet.label = first.group_name.empty() ? id : first.group_name;
        recordSet.table = first.table;
        recordSet.timeDomain = first.domain;
        for (const sensordata::StreamInfo &column : columns) {
            recordSet.columns.push_back(column.value_column);
        }
        recordSet.timeSource = first.handle;
        loaded.recordSets.push_back(std::move(recordSet));
    }

    for (const StreamMeta &stream : loaded.streams) {
        if (stream.timeDomain == sensordata::TimeDomain::ElapsedSeconds) {
            loaded.timeDomain = sensordata::TimeDomain::ElapsedSeconds;
        }
    }
    for (const RecordSet &recordSet : loaded.recordSets) {
        if (recordSet.timeDomain == sensordata::TimeDomain::ElapsedSeconds) {
            loaded.timeDomain = sensordata::TimeDomain::ElapsedSeconds;
        }
    }
    if (!loadImuCollectionStart(db, loaded, error) || !loadImuEvents(db, loaded, error)) {
        return false;
    }
    inferEpochCollectionStart(loaded);
    if ((tableExists(db, "Compass") || tableExists(db, "Calibration"))
        && !loadCompassCalibration(db, loaded, error)) {
        return false;
    }
    applyCalibrationToImuMagnetometer(loaded);
    applyCalibrationToImuAccelerometer(loaded);

    if (loaded.streams.empty() && loaded.recordSets.empty()) {
        error = "No supported sensor streams or record sets found in database";
        return false;
    }
    catalog = std::move(loaded);
    return true;
}

} // namespace sensorviz
