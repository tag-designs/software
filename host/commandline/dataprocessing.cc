/**
 * @file    dataprocessing.cc
 * @brief   Materialize derived sensor streams into copied SQLite logs.
 *
 * @details Copies an input SQLite log to a separate output database, adds
 *          processing provenance, and can materialize CompassTag calibrated
 *          vectors and canonical orientation streams, and IMUTag canonical
 *          orientation streams. Orientation is stored as a quaternion; Euler
 *          angles are deliberately not stored, because their values depend on
 *          a rotation-order convention that belongs to the display. Heading,
 *          declination and battery-forward direction are derived downstream
 *          from the quaternion.
 *
 *          The tool does not use Qt. The compass math is the Qt-free
 *          sensoranalysis core (compass_core.h, calibration_json.h), which is
 *          bit-identical to the Qt classes the applications use, so a log
 *          processed here matches what SensorViz derives from the same rows.
 */

#include "calibration_json.h"
#include "compass_core.h"
#include "cxxopts.hpp"
#include "imutag_log_format.h"
#include "recovery/sha256.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace
{

using compasscore::CalibrationConstants;
using compasscore::Orientation;
using sensormath::Vec3f;

/// Reported by --version and stored as ProcessingRun.ToolVersion.
constexpr const char *kToolVersion = "0.1";

// Version 2 dropped the yaw, pitch and roll columns from the orientation
// tables; orientation is the quaternion alone.
constexpr int kProcessorVersion = 2;

enum class IfExists
{
    Fail,
    Replace,
    Keep,
};

/**
 * @struct  ToolOptions
 * @brief   Parsed command-line options for one DataProcessing run.
 */
struct ToolOptions
{
    std::string inputPath;
    std::string outputPath;
    std::vector<std::string> processors;
    IfExists ifExists = IfExists::Fail;
    bool dryRun = false;
    bool listProcessors = false;
    std::string describeProcessor;
    bool printSummary = false;
};

/**
 * @struct  CompassSample
 * @brief   One raw CompassTag row from the input SQLite log.
 */
struct CompassSample
{
    int64_t epoch = 0;
    Vec3f accel;
    Vec3f mag;
};

/**
 * @struct  CompassInput
 * @brief   Loaded CompassTag samples and derived values for processors.
 *
 * @details rawSamples, calibratedMag and derived are parallel: entry i of each
 *          belongs to the same Compass row.
 */
struct CompassInput
{
    int64_t calibrationEpoch = 0;
    CalibrationConstants calibration;
    std::vector<CompassSample> rawSamples;
    std::vector<Vec3f> calibratedMag;
    std::vector<Orientation> derived;
};

/**
 * @struct  ImuCompassSample
 * @brief   One IMUTag magnetometer row paired with its superframe's acceleration.
 */
struct ImuCompassSample
{
    int64_t segmentId = 0;      ///< ImuMag.SegmentId.
    int64_t sampleIndex = 0;    ///< ImuMag.SampleIndex: first IMU sample of the superframe.
    int64_t elapsedUs = 0;      ///< ImuMag.ElapsedUs, corrected elapsed microseconds.
    int accelSamples = 0;       ///< ImuAccel rows averaged into the acceleration.
    Orientation derived;        ///< Orientation from the calibrated pair.
};

/**
 * @struct  ImuCompassInput
 * @brief   Loaded IMUTag compass pairs and the counts of rows left out.
 */
struct ImuCompassInput
{
    int64_t calibrationEpoch = 0;
    CalibrationConstants calibration;
    std::vector<ImuCompassSample> samples;
    int64_t magRows = 0;           ///< ImuMag rows read.
    int64_t skippedNoAccel = 0;    ///< Magnetometer rows whose superframe had no ImuAccel rows.
    int64_t skippedUnsolvable = 0; ///< Pairs the solve refused (a zero vector).
};

/**
 * @struct  StreamDefinition
 * @brief   Metadata row inserted into the SQLite streams catalog.
 */
struct StreamDefinition
{
    const char *id;
    const char *groupId;
    const char *groupName;
    const char *table;
    const char *timeColumn;
    const char *valueColumn;
    const char *kind;
    const char *displayName;
    const char *units;
    const char *quantity;
    const char *comment;
};

/**
 * @brief   Derives one CompassTag-style sample: offset-corrected acceleration
 *          and the magnetometer calibrated, then solved.
 *
 * @details The same steps, in the same order, as
 *          CompassProcessor::deriveSample() with calibration applied.
 *
 * @param[in]  calibration Constants to apply.
 * @param[in]  accel       Raw accelerometer vector, mg.
 * @param[in]  mag         Raw magnetometer vector, uT.
 * @param[out] derived     Orientation on success.
 *
 * @return  false when the pair cannot be solved (a zero vector).
 */
bool deriveOrientation(
    const CalibrationConstants &calibration,
    Vec3f accel,
    Vec3f mag,
    Orientation &derived)
{
    return compasscore::solveOrientation(
        mag, compasscore::applyAccelerometer(calibration, accel), &calibration, derived);
}

/**
 * @class   Database
 * @brief   Small RAII wrapper around one sqlite3 handle.
 */
class Database
{
public:
    Database(const std::string &path, int flags)
    {
        if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
            lastError_ = errorString();
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

    std::string lastError() const
    {
        return lastError_.empty() ? errorString() : lastError_;
    }

    bool exec(const std::string &sql, std::string &error)
    {
        char *sqliteError = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &sqliteError) != SQLITE_OK) {
            error = sqliteError ? sqliteError : sqlite3_errmsg(db_);
            sqlite3_free(sqliteError);
            return false;
        }
        return true;
    }

private:
    std::string errorString() const
    {
        return db_ ? sqlite3_errmsg(db_) : "database is not open";
    }

    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    std::string lastError_;
};

/**
 * @class   Statement
 * @brief   RAII wrapper around one prepared sqlite3 statement.
 */
class Statement
{
public:
    Statement(Database &db, const std::string &sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            lastError_ = errorString();
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

    std::string lastError() const
    {
        return lastError_;
    }

    bool bindInt64(int index, int64_t value)
    {
        return check(sqlite3_bind_int64(stmt_, index, value));
    }

    bool bindDouble(int index, double value)
    {
        return check(sqlite3_bind_double(stmt_, index, value));
    }

    bool bindText(int index, const std::string &value)
    {
        return check(sqlite3_bind_text(
            stmt_,
            index,
            value.data(),
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT));
    }

    bool bindNullableText(int index, const char *value)
    {
        if (!value) {
            return check(sqlite3_bind_null(stmt_, index));
        }
        return bindText(index, value);
    }

    int step()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            lastError_ = errorString();
        }
        return rc;
    }

    bool stepDone()
    {
        const int rc = step();
        if (rc != SQLITE_DONE) {
            if (lastError_.empty()) {
                lastError_ = "statement returned rows unexpectedly";
            }
            return false;
        }
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
        return true;
    }

    int64_t int64Column(int index) const
    {
        return sqlite3_column_int64(stmt_, index);
    }

    double doubleColumn(int index) const
    {
        return sqlite3_column_double(stmt_, index);
    }

    std::string textColumn(int index) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, index);
        return text ? reinterpret_cast<const char *>(text) : "";
    }

private:
    bool check(int rc)
    {
        if (rc != SQLITE_OK) {
            lastError_ = errorString();
            return false;
        }
        return true;
    }

    std::string errorString() const
    {
        return db_ ? sqlite3_errmsg(db_) : "database is not open";
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    std::string lastError_;
};

// ---------------------------------------------------------------------------
// Provenance JSON
// ---------------------------------------------------------------------------
//
// ProcessingRun stores small fixed JSON documents. They are written by hand in
// the form QJsonDocument::Compact produced before this tool left Qt -- keys in
// sorted order, no whitespace -- so provenance from either build reads alike.

/// Encodes @p text as a JSON string literal, escaping as QJsonDocument does.
std::string jsonString(const std::string &text)
{
    static const char *const hex = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (u < 0x20) {
                out += "\\u00";
                out += hex[u >> 4];
                out += hex[u & 0xf];
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

/// Encodes a list of strings as a compact JSON array.
std::string jsonArray(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); i++) {
        out += (i ? "," : "") + jsonString(items[i]);
    }
    return out + "]";
}

/**
 * @class   JsonObject
 * @brief   Compact JSON object writer with keys in sorted order.
 */
class JsonObject
{
public:
    void insert(const std::string &key, const std::string &text)
    {
        fields_[key] = jsonString(text);
    }

    void insert(const std::string &key, const char *text)
    {
        fields_[key] = jsonString(text);
    }

    void insert(const std::string &key, int64_t value)
    {
        fields_[key] = std::to_string(value);
    }

    void insert(const std::string &key, int value)
    {
        fields_[key] = std::to_string(value);
    }

    void insert(const std::string &key, const JsonObject &object)
    {
        fields_[key] = object.compact();
    }

    std::string compact() const
    {
        std::string out = "{";
        bool first = true;
        for (const auto &[key, value] : fields_) {
            out += (first ? "" : ",") + jsonString(key) + ":" + value;
            first = false;
        }
        return out + "}";
    }

private:
    std::map<std::string, std::string> fields_; ///< Key to already-encoded value.
};

/**
 * @brief   Calculates a SHA-256 hash for the source SQLite file.
 *
 * @param[in] path    File to hash.
 * @param[out] error  Populated with an explanatory message on failure.
 *
 * @return  Lowercase hexadecimal SHA-256 digest, or an empty string if the
 *          file could not be opened or read.
 */
std::string sha256File(const std::string &path, std::string &error)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        error = "Could not open " + path;
        return {};
    }

    tagcore::recovery::Sha256 hash;
    std::vector<char> buffer(1024 * 1024);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = file.gcount();
        if (got > 0) {
            hash.Update(reinterpret_cast<const uint8_t *>(buffer.data()), static_cast<size_t>(got));
        }
    }
    if (file.bad()) {
        error = "Could not read " + path;
        return {};
    }
    return hash.HexDigest();
}

/// Current UTC time as ISO 8601 with a Z suffix, as Qt::ISODate wrote it.
std::string currentUtcIso8601()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

/**
 * @brief   Checks whether a SQLite table exists.
 *
 * @param[in,out] db   Open database handle.
 * @param[in] name     Table name to find in @c sqlite_master.
 * @param[out] exists  Set to true when the table exists.
 * @param[out] error   Populated with the SQLite error text on failure.
 *
 * @return  true when the catalog query completed, false on SQLite failure.
 */
bool tableExists(Database &db, const std::string &name, bool &exists, std::string &error)
{
    Statement stmt(
        db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
    if (!stmt.valid() || !stmt.bindText(1, name)) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (rc == SQLITE_DONE) {
        exists = false;
        return true;
    }
    error = stmt.lastError();
    return false;
}

/**
 * @brief   Checks whether a stream metadata row exists.
 *
 * @param[in,out] db   Open database handle.
 * @param[in] id       Stream id to search for in @c streams.
 * @param[out] exists  Set to true when the stream id exists.
 * @param[out] error   Populated with the SQLite error text on failure.
 *
 * @return  true when the query completed, false on SQLite failure.
 */
bool streamExists(Database &db, const std::string &id, bool &exists, std::string &error)
{
    Statement stmt(db, "SELECT 1 FROM streams WHERE stream_id=? LIMIT 1");
    if (!stmt.valid() || !stmt.bindText(1, id)) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (rc == SQLITE_DONE) {
        exists = false;
        return true;
    }
    error = stmt.lastError();
    return false;
}

/**
 * @brief   Counts rows in a known SQLite table.
 *
 * @param[in,out] db  Open database handle.
 * @param[in] table   Trusted table name selected by the caller.
 * @param[out] count  Number of rows reported by SQLite.
 * @param[out] error  Populated with the SQLite error text on failure.
 *
 * @return  true when the count query completed, false on SQLite failure.
 *
 * @warning @p table is interpolated into SQL. Pass only fixed internal table
 *          names, not user-provided strings.
 */
bool rowCount(Database &db, const char *table, int64_t &count, std::string &error)
{
    Statement stmt(db, std::string("SELECT COUNT(*) FROM ") + table);
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc != SQLITE_ROW) {
        error = stmt.lastError();
        return false;
    }
    count = stmt.int64Column(0);
    return true;
}

/// IMUTag processor id; the others read the CompassTag Compass table.
const std::string kImuCompassOrientation = "imu-compass-orientation";

std::vector<std::string> processorIds()
{
    return {
        "compass-calibrated",
        "compass-orientation",
        kImuCompassOrientation,
    };
}

/// Whether a processor reads IMUTag tables rather than the Compass table.
bool isImuProcessor(const std::string &processor)
{
    return processor == kImuCompassOrientation;
}

/**
 * @brief   Returns the user-facing description for a processor id.
 *
 * @param[in] processor   Processor id from the command line.
 *
 * @return  Description text, or an empty string if the id is unknown.
 */
std::string processorDescription(const std::string &processor)
{
    if (processor == "compass-calibrated") {
        return "Writes CompassCalibrated with raw acceleration and calibrated "
               "magnetometer x/y/z columns, plus record-column stream metadata.";
    }
    if (processor == "compass-orientation") {
        return "Writes CompassOrientation with the magnetic-frame orientation "
               "quaternion, dip, field, and acceleration magnitude columns.";
    }
    if (processor == kImuCompassOrientation) {
        return "Writes ImuCompassOrientation for IMUTag logs: each ImuMag row is "
               "paired with the mean of its superframe's ImuAccel rows, calibrated, "
               "and solved for the magnetic-frame orientation quaternion, dip, field, "
               "and acceleration magnitude columns on ElapsedUs.";
    }
    return {};
}

/**
 * @brief   Tests whether a database has the inputs required by Compass processors.
 *
 * @param[in,out] db  Open SQLite log database.
 * @param[out] reason Success details or the first missing requirement.
 *
 * @return  true when raw compass samples and calibration rows are available.
 */
bool compassInputsAvailable(Database &db, std::string &reason)
{
    std::string error;
    bool hasCompass = false;
    bool hasCalibration = false;
    if (!tableExists(db, "Compass", hasCompass, error)) {
        reason = error;
        return false;
    }
    if (!hasCompass) {
        reason = "missing Compass table";
        return false;
    }
    if (!tableExists(db, "Calibration", hasCalibration, error)) {
        reason = error;
        return false;
    }
    if (!hasCalibration) {
        reason = "missing Calibration table";
        return false;
    }

    int64_t compassRows = 0;
    int64_t calibrationRows = 0;
    if (!rowCount(db, "Compass", compassRows, error)
        || !rowCount(db, "Calibration", calibrationRows, error)) {
        reason = error;
        return false;
    }
    if (compassRows <= 0) {
        reason = "Compass table is empty";
        return false;
    }
    if (calibrationRows <= 0) {
        reason = "Calibration table is empty";
        return false;
    }

    reason = std::to_string(compassRows) + " Compass rows, "
        + std::to_string(calibrationRows) + " calibration rows";
    return true;
}

/**
 * @brief   Tests whether a database has the inputs the IMUTag compass processor needs.
 *
 * @param[in,out] db  Open SQLite log database.
 * @param[out] reason Success details or the first missing requirement.
 *
 * @return  true when ImuMag, ImuAccel and Calibration all exist and have rows.
 */
bool imuCompassInputsAvailable(Database &db, std::string &reason)
{
    std::string error;
    int64_t counts[3] = {0, 0, 0};
    const char *tables[3] = {"ImuMag", "ImuAccel", "Calibration"};
    for (int i = 0; i < 3; i++) {
        bool exists = false;
        if (!tableExists(db, tables[i], exists, error)) {
            reason = error;
            return false;
        }
        if (!exists) {
            reason = std::string("missing ") + tables[i] + " table";
            return false;
        }
        if (!rowCount(db, tables[i], counts[i], error)) {
            reason = error;
            return false;
        }
        if (counts[i] <= 0) {
            reason = std::string(tables[i]) + " table is empty";
            return false;
        }
    }

    reason = std::to_string(counts[0]) + " ImuMag rows, " + std::to_string(counts[1])
        + " ImuAccel rows, " + std::to_string(counts[2]) + " calibration rows";
    return true;
}

/**
 * @brief   Tests whether a database has the inputs a given processor needs.
 *
 * @param[in,out] db      Open SQLite log database.
 * @param[in] processor   Processor id.
 * @param[out] reason     Success details or the first missing requirement.
 *
 * @return  true when the processor can run on this database.
 */
bool processorInputsAvailable(Database &db, const std::string &processor, std::string &reason)
{
    return isImuProcessor(processor) ? imuCompassInputsAvailable(db, reason)
                                     : compassInputsAvailable(db, reason);
}

/**
 * @brief   Reads the newest Calibration row.
 *
 * @details Parsed by compasscore::calibrationFromJson(), which reads the
 *          numbers exactly as the Qt applications do.
 *
 * @param[in,out] db          Open SQLite log database.
 * @param[out] epoch          The row's Epoch, in log units.
 * @param[out] calibration    Magnetometer and accelerometer constants from it.
 * @param[out] error          Populated when the row is missing or malformed.
 *
 * @return  true when the newest row parsed and holds a magnetometer object.
 */
bool loadLatestCalibration(
    Database &db,
    int64_t &epoch,
    CalibrationConstants &calibration,
    std::string &error)
{
    Statement row(
        db,
        "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");
    if (!row.valid()) {
        error = row.lastError();
        return false;
    }
    if (row.step() != SQLITE_ROW) {
        error = "Calibration table is empty";
        return false;
    }

    epoch = row.int64Column(0);
    std::string reason;
    if (!compasscore::calibrationFromJson(row.textColumn(1), calibration, reason)) {
        error = "Latest " + reason;
        return false;
    }
    return true;
}

/**
 * @brief   Loads raw compass rows, latest calibration constants, and derived samples.
 *
 * @param[in,out] db   Open SQLite log database.
 * @param[out] input   Populated CompassTag samples and derived values.
 * @param[out] error   Populated with the reason loading or derivation failed.
 *
 * @return  true when all CompassTag processor inputs were loaded.
 */
bool loadCompassInput(Database &db, CompassInput &input, std::string &error)
{
    std::string reason;
    if (!compassInputsAvailable(db, reason)) {
        error = reason;
        return false;
    }

    if (!loadLatestCalibration(db, input.calibrationEpoch, input.calibration, error)) {
        return false;
    }

    Statement rows(db, "SELECT Epoch, ax, ay, az, mx, my, mz FROM Compass ORDER BY Epoch");
    if (!rows.valid()) {
        error = rows.lastError();
        return false;
    }

    input.rawSamples.clear();
    input.calibratedMag.clear();
    input.derived.clear();
    while (true) {
        const int rc = rows.step();
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            error = rows.lastError();
            return false;
        }

        // Columns are read as double and stored as float, as QVector3D did.
        CompassSample sample;
        sample.epoch = rows.int64Column(0);
        sample.accel = Vec3f{
            static_cast<float>(rows.doubleColumn(1)),
            static_cast<float>(rows.doubleColumn(2)),
            static_cast<float>(rows.doubleColumn(3))};
        sample.mag = Vec3f{
            static_cast<float>(rows.doubleColumn(4)),
            static_cast<float>(rows.doubleColumn(5)),
            static_cast<float>(rows.doubleColumn(6))};

        Orientation derived;
        if (!deriveOrientation(input.calibration, sample.accel, sample.mag, derived)) {
            error = "Could not derive orientation at epoch " + std::to_string(sample.epoch);
            return false;
        }

        input.rawSamples.push_back(sample);
        input.calibratedMag.push_back(compasscore::applyMagnetometer(input.calibration, sample.mag));
        input.derived.push_back(derived);
    }

    if (input.rawSamples.empty()) {
        error = "Compass table is empty";
        return false;
    }
    return true;
}

/**
 * @brief   Pairs IMUTag magnetometer rows with acceleration and derives orientation.
 *
 * @details The magnetometer is logged once per superframe of
 *          IMUTAG_IMU_SAMPLES_PER_SUPERFRAME IMU samples, and an ImuMag row's
 *          SampleIndex is the first IMU sample index of its superframe. Each
 *          row is paired with the mean of the ImuAccel rows in the same segment
 *          whose SampleIndex lies in [SampleIndex, SampleIndex +
 *          IMUTAG_IMU_SAMPLES_PER_SUPERFRAME). Averaging matches the
 *          acceleration to the span the magnetometer value stands for and
 *          suppresses motion in the gravity estimate; it also means the
 *          acceleration is centred about half a superframe after the row's
 *          ElapsedUs. Both tables are read once, in (SegmentId, SampleIndex)
 *          order, and merged.
 *
 *          The pair is then derived with the newest calibration, which applies
 *          the magnetometer constants and the accelerometer zero-g offset,
 *          exactly as for CompassTag.
 *
 * @param[in,out] db     Open SQLite log database.
 * @param[out] input     Paired samples and the counts of rows left out.
 * @param[out] error     Populated with the reason loading failed.
 *
 * @return  true when the inputs loaded and at least one pair was solved. Rows
 *          with no acceleration in their superframe, or that cannot be solved,
 *          are counted and skipped rather than failing the run.
 */
bool loadImuCompassInput(Database &db, ImuCompassInput &input, std::string &error)
{
    std::string reason;
    if (!imuCompassInputsAvailable(db, reason)) {
        error = reason;
        return false;
    }
    if (!loadLatestCalibration(db, input.calibrationEpoch, input.calibration, error)) {
        return false;
    }

    Statement mag(
        db,
        "SELECT SegmentId, SampleIndex, ElapsedUs, mx, my, mz FROM ImuMag "
        "ORDER BY SegmentId, SampleIndex");
    Statement accel(
        db,
        "SELECT SegmentId, SampleIndex, ax, ay, az FROM ImuAccel "
        "ORDER BY SegmentId, SampleIndex");
    if (!mag.valid()) {
        error = mag.lastError();
        return false;
    }
    if (!accel.valid()) {
        error = accel.lastError();
        return false;
    }

    // The accelerometer cursor only moves forward; superframes do not overlap.
    bool haveAccel = false;
    bool accelDone = false;
    int64_t accelSegment = 0;
    int64_t accelIndex = 0;
    Vec3f accelValue;
    auto nextAccel = [&]() -> bool {
        const int rc = accel.step();
        if (rc == SQLITE_DONE) {
            haveAccel = false;
            accelDone = true;
            return true;
        }
        if (rc != SQLITE_ROW) {
            error = accel.lastError();
            return false;
        }
        haveAccel = true;
        accelSegment = accel.int64Column(0);
        accelIndex = accel.int64Column(1);
        accelValue = Vec3f{
            static_cast<float>(accel.doubleColumn(2)),
            static_cast<float>(accel.doubleColumn(3)),
            static_cast<float>(accel.doubleColumn(4))};
        return true;
    };
    if (!nextAccel()) {
        return false;
    }

    input.samples.clear();
    input.magRows = 0;
    input.skippedNoAccel = 0;
    input.skippedUnsolvable = 0;
    while (true) {
        const int rc = mag.step();
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            error = mag.lastError();
            return false;
        }
        input.magRows++;

        ImuCompassSample sample;
        sample.segmentId = mag.int64Column(0);
        sample.sampleIndex = mag.int64Column(1);
        sample.elapsedUs = mag.int64Column(2);
        const Vec3f field{
            static_cast<float>(mag.doubleColumn(3)),
            static_cast<float>(mag.doubleColumn(4)),
            static_cast<float>(mag.doubleColumn(5))};
        const int64_t end = sample.sampleIndex + IMUTAG_IMU_SAMPLES_PER_SUPERFRAME;

        // Skip acceleration before this superframe, then sum the rows inside it.
        while (!accelDone
               && (accelSegment < sample.segmentId
                   || (accelSegment == sample.segmentId && accelIndex < sample.sampleIndex))) {
            if (!nextAccel()) {
                return false;
            }
        }
        Vec3f sum;
        while (haveAccel && accelSegment == sample.segmentId && accelIndex < end) {
            sum += accelValue;
            sample.accelSamples++;
            if (!nextAccel()) {
                return false;
            }
        }
        if (sample.accelSamples == 0) {
            input.skippedNoAccel++;
            continue;
        }

        const Vec3f meanAccel = sum / static_cast<float>(sample.accelSamples);
        if (!deriveOrientation(input.calibration, meanAccel, field, sample.derived)) {
            input.skippedUnsolvable++;
            continue;
        }
        input.samples.push_back(sample);
    }

    if (input.samples.empty()) {
        error = "No ImuMag row could be paired with acceleration ("
            + std::to_string(input.magRows) + " rows read)";
        return false;
    }
    return true;
}

/**
 * @brief   Removes existing stream metadata rows for a processor output.
 *
 * @param[in,out] db      Open processed database.
 * @param[in] streamIds   Stream ids owned by the selected processor.
 * @param[out] error      Populated with SQLite error text on failure.
 *
 * @return  true when every requested row was deleted or did not exist.
 */
bool deleteStreamIds(Database &db, const std::vector<std::string> &streamIds, std::string &error)
{
    Statement stmt(db, "DELETE FROM streams WHERE stream_id=?");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }
    for (const std::string &id : streamIds) {
        if (!stmt.bindText(1, id) || !stmt.stepDone()) {
            error = stmt.lastError();
            return false;
        }
    }
    return true;
}

/**
 * @brief   Inserts stream metadata rows for newly materialized columns.
 *
 * @param[in,out] db   Open processed database.
 * @param[in] streams  Stream definitions owned by one processor.
 * @param[out] error   Populated with SQLite error text on failure.
 *
 * @return  true when all stream rows were inserted.
 */
bool insertStreamDefinitions(
    Database &db,
    const std::vector<StreamDefinition> &streams,
    std::string &error)
{
    Statement stmt(
        db,
        "INSERT INTO streams ("
        "stream_id, group_id, group_name, table_name, time_column, value_column, "
        "stream_kind, display_name, units, quantity, comment"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }

    for (const StreamDefinition &stream : streams) {
        if (!stmt.bindText(1, stream.id)
            || !stmt.bindNullableText(2, stream.groupId)
            || !stmt.bindNullableText(3, stream.groupName)
            || !stmt.bindText(4, stream.table)
            || !stmt.bindText(5, stream.timeColumn)
            || !stmt.bindText(6, stream.valueColumn)
            || !stmt.bindText(7, stream.kind)
            || !stmt.bindText(8, stream.displayName)
            || !stmt.bindNullableText(9, stream.units)
            || !stmt.bindNullableText(10, stream.quantity)
            || !stmt.bindNullableText(11, stream.comment)
            || !stmt.stepDone()) {
            error = stmt.lastError();
            return false;
        }
    }
    return true;
}

/**
 * @brief   Ensures the provenance table exists in the processed database.
 *
 * @param[in,out] db  Open processed database.
 * @param[out] error  Populated with SQLite error text on failure.
 *
 * @return  true when the table exists or was created.
 */
bool createProcessingRunTable(Database &db, std::string &error)
{
    return db.exec(
        "CREATE TABLE IF NOT EXISTS ProcessingRun ("
        "RunId INTEGER PRIMARY KEY,"
        "ToolName TEXT,"
        "ToolVersion TEXT,"
        "ProcessorId TEXT,"
        "ProcessorVersion INTEGER,"
        "CreatedUtc TEXT,"
        "InputFileName TEXT,"
        "InputSha256 TEXT,"
        "ConfigurationJson TEXT,"
        "SourceTablesJson TEXT,"
        "OutputTablesJson TEXT,"
        "Status TEXT"
        ");",
        error);
}

/**
 * @brief   Records one successful processor invocation.
 *
 * @param[in,out] db       Open processed database.
 * @param[in] options      Parsed command-line options for the run.
 * @param[in] inputHash    SHA-256 digest of the original input file.
 * @param[in] processor    Processor id that produced the outputs.
 * @param[in] calibrationEpoch Epoch of the calibration row used, in log units.
 * @param[in] sourceTables Tables read by the processor.
 * @param[in] outputTables Tables written by the processor.
 * @param[out] error       Populated with SQLite error text on failure.
 *
 * @return  true when the provenance row was inserted.
 */
bool insertProcessingRun(
    Database &db,
    const ToolOptions &options,
    const std::string &inputHash,
    const std::string &processor,
    int64_t calibrationEpoch,
    const std::vector<std::string> &sourceTables,
    const std::vector<std::string> &outputTables,
    std::string &error)
{
    JsonObject calibrationSource;
    calibrationSource.insert("table", "Calibration");
    calibrationSource.insert("epoch", calibrationEpoch);

    JsonObject configuration;
    configuration.insert("processor", processor);
    configuration.insert("algorithm_version", kProcessorVersion);
    configuration.insert("calibration_source", calibrationSource);
    if (processor == "compass-orientation" || processor == kImuCompassOrientation) {
        configuration.insert("orientation_frame", "magnetic-frame-nwu");
        configuration.insert("quaternion_order", "wxyz");
        configuration.insert(
            "heading_policy",
            "not_materialized; downstream tools derive heading from the quaternion, "
            "then apply declination and mounting convention");
    }
    if (processor == kImuCompassOrientation) {
        JsonObject pairing;
        pairing.insert("method", "superframe_mean");
        pairing.insert("samples_per_superframe", IMUTAG_IMU_SAMPLES_PER_SUPERFRAME);
        pairing.insert(
            "description",
            "each ImuMag row uses the mean of ImuAccel rows in its segment with "
            "SampleIndex in [SampleIndex, SampleIndex + samples_per_superframe)");
        configuration.insert("accel_pairing", pairing);
    }

    Statement stmt(
        db,
        "INSERT INTO ProcessingRun ("
        "ToolName, ToolVersion, ProcessorId, ProcessorVersion, CreatedUtc, "
        "InputFileName, InputSha256, ConfigurationJson, SourceTablesJson, "
        "OutputTablesJson, Status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }

    if (!stmt.bindText(1, "dataprocessing")
        || !stmt.bindText(2, kToolVersion)
        || !stmt.bindText(3, processor)
        || !stmt.bindInt64(4, kProcessorVersion)
        || !stmt.bindText(5, currentUtcIso8601())
        || !stmt.bindText(6, std::filesystem::path(options.inputPath).filename().string())
        || !stmt.bindText(7, inputHash)
        || !stmt.bindText(8, configuration.compact())
        || !stmt.bindText(9, jsonArray(sourceTables))
        || !stmt.bindText(10, jsonArray(outputTables))
        || !stmt.bindText(11, "success")
        || !stmt.stepDone()) {
        error = stmt.lastError();
        return false;
    }
    return true;
}

std::vector<std::string> streamIdsForProcessor(const std::string &processor)
{
    if (processor == "compass-calibrated") {
        return {
            "compass_calibrated_ax",
            "compass_calibrated_ay",
            "compass_calibrated_az",
            "compass_calibrated_mx",
            "compass_calibrated_my",
            "compass_calibrated_mz",
        };
    }
    // The yaw, pitch and roll ids are no longer written (processor version 2)
    // but stay listed, so that replacing an older output removes their rows.
    if (processor == "compass-orientation") {
        return {
            "compass_orientation_yaw",
            "compass_orientation_pitch",
            "compass_orientation_roll",
            "compass_orientation_qw",
            "compass_orientation_qx",
            "compass_orientation_qy",
            "compass_orientation_qz",
            "compass_orientation_dip",
            "compass_orientation_field",
            "compass_orientation_acceleration",
        };
    }
    if (processor == kImuCompassOrientation) {
        return {
            "imu_compass_orientation_yaw",
            "imu_compass_orientation_pitch",
            "imu_compass_orientation_roll",
            "imu_compass_orientation_qw",
            "imu_compass_orientation_qx",
            "imu_compass_orientation_qy",
            "imu_compass_orientation_qz",
            "imu_compass_orientation_dip",
            "imu_compass_orientation_field",
            "imu_compass_orientation_acceleration",
        };
    }
    return {};
}

std::vector<std::string> tableNamesForProcessor(const std::string &processor)
{
    if (processor == "compass-calibrated") {
        return {"CompassCalibrated"};
    }
    if (processor == "compass-orientation") {
        return {"CompassOrientation"};
    }
    if (processor == kImuCompassOrientation) {
        return {"ImuCompassOrientation"};
    }
    return {};
}

/**
 * @brief   Applies the selected overwrite policy to a processor's outputs.
 *
 * @param[in,out] db   Open processed database inside the write transaction.
 * @param[in] processor Processor id whose tables and streams are being prepared.
 * @param[in] policy   User-selected conflict policy.
 * @param[out] skip    Set when @c IfExists::Keep leaves existing output intact.
 * @param[out] error   Populated with the conflict or SQLite failure reason.
 *
 * @return  true when output names are ready for writing or should be skipped.
 */
bool prepareProcessorOutputs(
    Database &db,
    const std::string &processor,
    IfExists policy,
    bool &skip,
    std::string &error)
{
    skip = false;
    bool conflict = false;
    for (const std::string &table : tableNamesForProcessor(processor)) {
        bool exists = false;
        if (!tableExists(db, table, exists, error)) {
            return false;
        }
        conflict = conflict || exists;
    }
    for (const std::string &id : streamIdsForProcessor(processor)) {
        bool exists = false;
        if (!streamExists(db, id, exists, error)) {
            return false;
        }
        conflict = conflict || exists;
    }

    if (conflict && policy == IfExists::Fail) {
        error = "Output for " + processor + " already exists; use --if-exists replace or keep";
        return false;
    }
    if (conflict && policy == IfExists::Keep) {
        skip = true;
        return true;
    }

    if (policy == IfExists::Replace || conflict) {
        for (const std::string &table : tableNamesForProcessor(processor)) {
            if (!db.exec("DROP TABLE IF EXISTS " + table, error)) {
                return false;
            }
        }
        if (!deleteStreamIds(db, streamIdsForProcessor(processor), error)) {
            return false;
        }
    }
    return true;
}

std::vector<StreamDefinition> calibratedStreamDefinitions()
{
    return {
        {"compass_calibrated_ax", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "ax", "record_column", "Acceleration X",
         "mg", "acceleration_x", "Accelerometer X with the calibrated zero-g offset removed."},
        {"compass_calibrated_ay", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "ay", "record_column", "Acceleration Y",
         "mg", "acceleration_y", "Accelerometer Y with the calibrated zero-g offset removed."},
        {"compass_calibrated_az", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "az", "record_column", "Acceleration Z",
         "mg", "acceleration_z", "Accelerometer Z with the calibrated zero-g offset removed."},
        {"compass_calibrated_mx", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "mx", "record_column", "Magnetic field X",
         "uT", "magnetic_field_x", "Calibrated magnetometer X sample."},
        {"compass_calibrated_my", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "my", "record_column", "Magnetic field Y",
         "uT", "magnetic_field_y", "Calibrated magnetometer Y sample."},
        {"compass_calibrated_mz", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "mz", "record_column", "Magnetic field Z",
         "uT", "magnetic_field_z", "Calibrated magnetometer Z sample."},
    };
}

std::vector<StreamDefinition> orientationStreamDefinitions()
{
    // One record set: SensorViz derives heading, pitch and roll from the
    // quaternion, and plots dip, field and acceleration beside them.
    return {
        {"compass_orientation_qw", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "qw",
         "record_column", "Quaternion w", nullptr, "quaternion_w", "Orientation quaternion, scalar part."},
        {"compass_orientation_qx", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "qx",
         "record_column", "Quaternion x", nullptr, "quaternion_x", "Orientation quaternion, x part."},
        {"compass_orientation_qy", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "qy",
         "record_column", "Quaternion y", nullptr, "quaternion_y", "Orientation quaternion, y part."},
        {"compass_orientation_qz", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "qz",
         "record_column", "Quaternion z", nullptr, "quaternion_z", "Orientation quaternion, z part."},
        {"compass_orientation_dip", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "dip",
         "record_column", "Dip", "deg", "angle", "Magnetic dip angle."},
        {"compass_orientation_field", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "field",
         "record_column", "Magnetic field", "uT", "magnetic_field", "Calibrated magnetic field magnitude."},
        {"compass_orientation_acceleration", "compass_orientation", "Compass orientation", "CompassOrientation", "Epoch", "acceleration",
         "record_column", "Acceleration", "mg", "acceleration", "Acceleration magnitude."},
    };
}

std::vector<StreamDefinition> imuOrientationStreamDefinitions()
{
    // One record set: SensorViz derives heading, pitch and roll from the
    // quaternion, and plots dip, field and acceleration beside them.
    return {
        {"imu_compass_orientation_qw", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "qw",
         "record_column", "Quaternion w", nullptr, "quaternion_w", "Orientation quaternion, scalar part."},
        {"imu_compass_orientation_qx", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "qx",
         "record_column", "Quaternion x", nullptr, "quaternion_x", "Orientation quaternion, x part."},
        {"imu_compass_orientation_qy", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "qy",
         "record_column", "Quaternion y", nullptr, "quaternion_y", "Orientation quaternion, y part."},
        {"imu_compass_orientation_qz", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "qz",
         "record_column", "Quaternion z", nullptr, "quaternion_z", "Orientation quaternion, z part."},
        {"imu_compass_orientation_dip", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "dip",
         "record_column", "Dip", "deg", "angle", "Magnetic dip angle."},
        {"imu_compass_orientation_field", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "field",
         "record_column", "Magnetic field", "uT", "magnetic_field", "Calibrated magnetic field magnitude."},
        {"imu_compass_orientation_acceleration", "imu_compass_orientation", "IMU compass orientation", "ImuCompassOrientation", "ElapsedUs", "acceleration",
         "record_column", "Acceleration", "mg", "acceleration", "Magnitude of the superframe-mean acceleration, zero-g offset removed."},
    };
}

/**
 * @brief   Writes calibrated CompassTag vector columns and stream metadata.
 *
 * @param[in,out] db     Open processed database inside the write transaction.
 * @param[in] options    Parsed command-line options for provenance fields.
 * @param[in] inputHash  SHA-256 digest of the original input file.
 * @param[in] input      Loaded CompassTag rows and calibrated vectors.
 * @param[out] error     Populated with SQLite error text on failure.
 *
 * @return  true when the output table, metadata, and provenance row were written.
 */
bool runCompassCalibrated(
    Database &db,
    const ToolOptions &options,
    const std::string &inputHash,
    const CompassInput &input,
    std::string &error)
{
    if (!db.exec(
            "CREATE TABLE CompassCalibrated ("
            "Epoch INTEGER,"
            "ax REAL,"
            "ay REAL,"
            "az REAL,"
            "mx REAL,"
            "my REAL,"
            "mz REAL"
            ");",
            error)) {
        return false;
    }

    Statement insert(
        db,
        "INSERT INTO CompassCalibrated (Epoch, ax, ay, az, mx, my, mz) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!insert.valid()) {
        error = insert.lastError();
        return false;
    }

    for (size_t i = 0; i < input.rawSamples.size(); i++) {
        const CompassSample &sample = input.rawSamples[i];
        const Vec3f mag = input.calibratedMag[i];
        const Vec3f accel = compasscore::applyAccelerometer(input.calibration, sample.accel);
        if (!insert.bindInt64(1, sample.epoch)
            || !insert.bindDouble(2, accel.x)
            || !insert.bindDouble(3, accel.y)
            || !insert.bindDouble(4, accel.z)
            || !insert.bindDouble(5, mag.x)
            || !insert.bindDouble(6, mag.y)
            || !insert.bindDouble(7, mag.z)
            || !insert.stepDone()) {
            error = insert.lastError();
            return false;
        }
    }

    if (!insertStreamDefinitions(db, calibratedStreamDefinitions(), error)) {
        return false;
    }
    return insertProcessingRun(
        db,
        options,
        inputHash,
        "compass-calibrated",
        input.calibrationEpoch,
        {"Compass", "Calibration"},
        {"CompassCalibrated"},
        error);
}

/**
 * @brief   Writes canonical CompassTag orientation columns and stream metadata.
 *
 * @param[in,out] db     Open processed database inside the write transaction.
 * @param[in] options    Parsed command-line options for provenance fields.
 * @param[in] inputHash  SHA-256 digest of the original input file.
 * @param[in] input      Loaded CompassTag rows and orientation samples.
 * @param[out] error     Populated with SQLite error text on failure.
 *
 * @return  true when the output table, metadata, and provenance row were written.
 */
bool runCompassOrientation(
    Database &db,
    const ToolOptions &options,
    const std::string &inputHash,
    const CompassInput &input,
    std::string &error)
{
    if (!db.exec(
            "CREATE TABLE CompassOrientation ("
            "Epoch INTEGER,"
            "dip REAL,"
            "field REAL,"
            "acceleration REAL,"
            "qw REAL,"
            "qx REAL,"
            "qy REAL,"
            "qz REAL"
            ");",
            error)) {
        return false;
    }

    Statement insert(
        db,
        "INSERT INTO CompassOrientation ("
        "Epoch, dip, field, acceleration, qw, qx, qy, qz"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    if (!insert.valid()) {
        error = insert.lastError();
        return false;
    }

    for (size_t i = 0; i < input.derived.size(); i++) {
        const Orientation &sample = input.derived[i];
        if (!insert.bindInt64(1, input.rawSamples[i].epoch)
            || !insert.bindDouble(2, sample.dip)
            || !insert.bindDouble(3, sample.field)
            || !insert.bindDouble(4, sample.mg)
            || !insert.bindDouble(5, sample.q.w)
            || !insert.bindDouble(6, sample.q.x)
            || !insert.bindDouble(7, sample.q.y)
            || !insert.bindDouble(8, sample.q.z)
            || !insert.stepDone()) {
            error = insert.lastError();
            return false;
        }
    }

    if (!insertStreamDefinitions(db, orientationStreamDefinitions(), error)) {
        return false;
    }
    return insertProcessingRun(
        db,
        options,
        inputHash,
        "compass-orientation",
        input.calibrationEpoch,
        {"Compass", "Calibration"},
        {"CompassOrientation"},
        error);
}

/**
 * @brief   Writes canonical IMUTag orientation columns and stream metadata.
 *
 * @param[in,out] db     Open processed database inside the write transaction.
 * @param[in] options    Parsed command-line options for provenance fields.
 * @param[in] inputHash  SHA-256 digest of the original input file.
 * @param[in] input      Paired IMUTag samples and their orientation.
 * @param[out] error     Populated with SQLite error text on failure.
 *
 * @return  true when the output table, metadata, and provenance row were written.
 */
bool runImuCompassOrientation(
    Database &db,
    const ToolOptions &options,
    const std::string &inputHash,
    const ImuCompassInput &input,
    std::string &error)
{
    if (!db.exec(
            "CREATE TABLE ImuCompassOrientation ("
            "SegmentId INTEGER,"
            "SampleIndex INTEGER,"
            "ElapsedUs INTEGER,"
            "dip REAL,"
            "field REAL,"
            "acceleration REAL,"
            "AccelSamples INTEGER,"
            "qw REAL,"
            "qx REAL,"
            "qy REAL,"
            "qz REAL"
            ");",
            error)) {
        return false;
    }

    Statement insert(
        db,
        "INSERT INTO ImuCompassOrientation ("
        "SegmentId, SampleIndex, ElapsedUs, dip, field, "
        "acceleration, AccelSamples, qw, qx, qy, qz"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!insert.valid()) {
        error = insert.lastError();
        return false;
    }

    for (const ImuCompassSample &sample : input.samples) {
        const Orientation &d = sample.derived;
        if (!insert.bindInt64(1, sample.segmentId)
            || !insert.bindInt64(2, sample.sampleIndex)
            || !insert.bindInt64(3, sample.elapsedUs)
            || !insert.bindDouble(4, d.dip)
            || !insert.bindDouble(5, d.field)
            || !insert.bindDouble(6, d.mg)
            || !insert.bindInt64(7, sample.accelSamples)
            || !insert.bindDouble(8, d.q.w)
            || !insert.bindDouble(9, d.q.x)
            || !insert.bindDouble(10, d.q.y)
            || !insert.bindDouble(11, d.q.z)
            || !insert.stepDone()) {
            error = insert.lastError();
            return false;
        }
    }

    if (!insertStreamDefinitions(db, imuOrientationStreamDefinitions(), error)) {
        return false;
    }
    return insertProcessingRun(
        db,
        options,
        inputHash,
        kImuCompassOrientation,
        input.calibrationEpoch,
        {"ImuMag", "ImuAccel", "Calibration"},
        {"ImuCompassOrientation"},
        error);
}

/// Prints the pairing counts for an IMUTag compass load.
void printImuCompassCounts(const ImuCompassInput &input)
{
    std::cout << "  " << input.samples.size() << " of " << input.magRows
              << " ImuMag rows paired";
    if (input.skippedNoAccel > 0) {
        std::cout << "; " << input.skippedNoAccel << " had no ImuAccel rows in their superframe";
    }
    if (input.skippedUnsolvable > 0) {
        std::cout << "; " << input.skippedUnsolvable << " could not be solved";
    }
    std::cout << "\n";
}

/**
 * @brief   Creates or reuses the output database according to conflict policy.
 *
 * @param[in] options  Parsed paths and output conflict policy.
 * @param[out] error   Populated with the filesystem failure or policy conflict.
 *
 * @return  true when @c options.outputPath is ready for writing.
 *
 * @post    For @c IfExists::Replace and missing outputs, the output path is a
 *          byte-for-byte copy of the input before processor writes begin.
 */
bool copyInputToOutput(const ToolOptions &options, std::string &error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    // Compared as absolute, lexically normal paths, as QFileInfo did: a
    // different spelling of the same file is caught, a symlink is not.
    const fs::path input = fs::absolute(options.inputPath, ec).lexically_normal();
    const fs::path output = fs::absolute(options.outputPath, ec).lexically_normal();
    if (input == output) {
        error = "Output path must be different from input path.";
        return false;
    }
    if (!fs::exists(input, ec)) {
        error = "Input file does not exist: " + options.inputPath;
        return false;
    }

    if (fs::exists(output, ec)) {
        if (options.ifExists == IfExists::Fail) {
            error = "Output file already exists: " + options.outputPath;
            return false;
        }
        if (options.ifExists == IfExists::Keep) {
            return true;
        }
        if (!fs::remove(output, ec)) {
            error = "Could not remove existing output file: " + options.outputPath;
            return false;
        }
    }

    if (!fs::copy_file(input, output, ec)) {
        error = "Could not copy " + input.string() + " to " + output.string() + ": " + ec.message();
        return false;
    }
    return true;
}

/**
 * @brief   Parses the output conflict policy option.
 *
 * @param[in] text     Command-line value.
 * @param[out] policy  Parsed enum value on success.
 *
 * @return  true for @c fail, @c replace, or @c keep.
 */
bool parseIfExists(const std::string &text, IfExists &policy)
{
    if (text == "fail") {
        policy = IfExists::Fail;
        return true;
    }
    if (text == "replace") {
        policy = IfExists::Replace;
        return true;
    }
    if (text == "keep") {
        policy = IfExists::Keep;
        return true;
    }
    return false;
}

/**
 * @brief   Parses DataProcessing command-line options.
 *
 * @details Handles --help and --version itself and exits. A malformed
 *          command line or an unknown --if-exists policy exits with code 2.
 *
 * @param[in] argc  Argument count from main().
 * @param[in] argv  Argument vector from main().
 *
 * @return  Parsed options.
 */
ToolOptions parseOptions(int argc, char **argv)
{
    cxxopts::Options parser("dataprocessing", "copy SQLite logs and materialize derived sensor streams");
    parser.add_options()
        ("i,input", "Input SQLite log path.", cxxopts::value<std::string>(), "path")
        ("o,output", "Output SQLite log path.", cxxopts::value<std::string>(), "path")
        ("p,processor", "Processor to run; repeat for multiple processors.",
         cxxopts::value<std::vector<std::string>>(), "id")
        ("if-exists", "Output conflict policy: fail, replace, or keep.",
         cxxopts::value<std::string>()->default_value("fail"), "policy")
        ("dry-run", "Validate inputs and print planned outputs without writing.")
        ("list-processors", "List processors and whether they are available for the input log.")
        ("describe", "Describe one processor.", cxxopts::value<std::string>(), "id")
        ("print-summary", "Print processing summary to stdout.")
        ("v,version", "Displays version information.")
        ("h,help", "Displays help on commandline options.");

    ToolOptions options;
    try {
        const cxxopts::ParseResult result = parser.parse(argc, argv);
        if (result.count("help")) {
            std::cout << parser.help() << "\n";
            std::exit(0);
        }
        if (result.count("version")) {
            std::cout << "dataprocessing " << kToolVersion << "\n";
            std::exit(0);
        }
        if (result.count("input")) {
            options.inputPath = result["input"].as<std::string>();
        }
        if (result.count("output")) {
            options.outputPath = result["output"].as<std::string>();
        }
        if (result.count("processor")) {
            options.processors = result["processor"].as<std::vector<std::string>>();
        }
        if (result.count("describe")) {
            options.describeProcessor = result["describe"].as<std::string>();
        }
        options.dryRun = result.count("dry-run") > 0;
        options.listProcessors = result.count("list-processors") > 0;
        options.printSummary = result.count("print-summary") > 0;
        const std::string policy = result["if-exists"].as<std::string>();
        if (!parseIfExists(policy, options.ifExists)) {
            std::cerr << "Unknown --if-exists policy: " << policy << "\n";
            std::exit(2);
        }
    } catch (const cxxopts::OptionException &e) {
        std::cerr << e.what() << "\n" << parser.help() << "\n";
        std::exit(2);
    }
    return options;
}

/**
 * @brief   Validates requested processor ids before opening the output database.
 *
 * @param[in] processors  Processor ids from repeated @c --processor options.
 * @param[out] error      Populated with the unknown id, if any.
 *
 * @return  true when every id is currently supported.
 */
bool validateProcessorIds(const std::vector<std::string> &processors, std::string &error)
{
    const std::vector<std::string> known = processorIds();
    for (const std::string &processor : processors) {
        if (std::find(known.begin(), known.end(), processor) == known.end()) {
            error = "Unknown processor " + processor;
            return false;
        }
    }
    return true;
}

/**
 * @brief   Prints supported processor ids and their availability for a log.
 *
 * @param[in,out] db  Open input SQLite database.
 */
void listProcessors(Database &db)
{
    for (const std::string &processor : processorIds()) {
        std::string reason;
        const bool available = processorInputsAvailable(db, processor, reason);
        std::cout << processor << ": "
                  << (available ? "available" : "unavailable")
                  << " (" << reason << ")\n";
    }
}

/**
 * @brief   Validates processor inputs and prints planned output tables.
 *
 * @param[in,out] db  Open input SQLite database.
 * @param[in] options Parsed command-line options.
 * @param[out] error  Populated with the validation failure reason.
 *
 * @return  true when all requested processors can run on the input.
 */
bool dryRunProcessors(Database &db, const ToolOptions &options, std::string &error)
{
    CompassInput input;
    ImuCompassInput imuInput;
    bool compassLoaded = false;
    bool imuLoaded = false;

    for (const std::string &processor : options.processors) {
        if (isImuProcessor(processor)) {
            if (!imuLoaded && !loadImuCompassInput(db, imuInput, error)) {
                return false;
            }
            imuLoaded = true;
            std::cout << "Would run " << processor << " on "
                      << imuInput.magRows << " ImuMag rows -> ImuCompassOrientation\n";
            printImuCompassCounts(imuInput);
            continue;
        }
        if (!compassLoaded && !loadCompassInput(db, input, error)) {
            return false;
        }
        compassLoaded = true;
        std::cout << "Would run " << processor << " on "
                  << input.rawSamples.size() << " Compass rows";
        if (processor == "compass-calibrated") {
            std::cout << " -> CompassCalibrated\n";
        } else if (processor == "compass-orientation") {
            std::cout << " -> CompassOrientation\n";
        } else {
            error = "Unknown processor " + processor;
            return false;
        }
    }
    return true;
}

/**
 * @brief   Copies the input database and executes requested processors.
 *
 * @param[in] options  Parsed command-line options.
 * @param[out] error   Populated with the first filesystem, input, or SQLite
 *                     failure encountered.
 *
 * @return  true when every requested processor was written and committed.
 */
bool runProcessors(const ToolOptions &options, std::string &error)
{
    const std::string inputHash = sha256File(options.inputPath, error);
    if (inputHash.empty()) {
        return false;
    }
    if (!copyInputToOutput(options, error)) {
        return false;
    }

    Database db(options.outputPath, SQLITE_OPEN_READWRITE);
    if (!db.isOpen()) {
        error = db.lastError();
        return false;
    }

    // Load each family's inputs once, and only when a requested processor needs
    // them, so an IMUTag log is not rejected for lacking a Compass table.
    CompassInput input;
    ImuCompassInput imuInput;
    bool needCompass = false;
    bool needImu = false;
    for (const std::string &processor : options.processors) {
        (isImuProcessor(processor) ? needImu : needCompass) = true;
    }
    if (needCompass && !loadCompassInput(db, input, error)) {
        return false;
    }
    if (needImu && !loadImuCompassInput(db, imuInput, error)) {
        return false;
    }

    if (!db.exec("BEGIN IMMEDIATE TRANSACTION", error)) {
        return false;
    }
    if (!createProcessingRunTable(db, error)) {
        db.exec("ROLLBACK", error);
        return false;
    }

    for (const std::string &processor : options.processors) {
        bool skip = false;
        if (!prepareProcessorOutputs(db, processor, options.ifExists, skip, error)) {
            db.exec("ROLLBACK", error);
            return false;
        }
        if (skip) {
            if (options.printSummary) {
                std::cout << "Kept existing outputs for " << processor << "\n";
            }
            continue;
        }

        bool ok = false;
        if (processor == "compass-calibrated") {
            ok = runCompassCalibrated(db, options, inputHash, input, error);
        } else if (processor == "compass-orientation") {
            ok = runCompassOrientation(db, options, inputHash, input, error);
        } else if (processor == kImuCompassOrientation) {
            ok = runImuCompassOrientation(db, options, inputHash, imuInput, error);
        } else {
            error = "Unknown processor " + processor;
        }
        if (!ok) {
            db.exec("ROLLBACK", error);
            return false;
        }
        if (options.printSummary) {
            std::cout << "Wrote " << processor << "\n";
            if (isImuProcessor(processor)) {
                printImuCompassCounts(imuInput);
            }
        }
    }

    return db.exec("COMMIT", error);
}

} // namespace

int main(int argc, char **argv)
{
    const ToolOptions options = parseOptions(argc, argv);
    std::string error;

    if (!options.describeProcessor.empty()) {
        const std::string description = processorDescription(options.describeProcessor);
        if (description.empty()) {
            std::cerr << "Unknown processor " << options.describeProcessor << "\n";
            return 2;
        }
        std::cout << options.describeProcessor << "\n" << description << "\n";
        return 0;
    }

    if (options.inputPath.empty()) {
        std::cerr << "--input is required\n";
        return 2;
    }

    Database inputDb(options.inputPath, SQLITE_OPEN_READONLY);
    if (!inputDb.isOpen()) {
        std::cerr << "Could not open input: " << inputDb.lastError() << "\n";
        return 1;
    }

    if (options.listProcessors) {
        listProcessors(inputDb);
        return 0;
    }

    if (options.processors.empty()) {
        std::cerr << "At least one --processor is required unless using --list-processors or --describe\n";
        return 2;
    }
    if (!validateProcessorIds(options.processors, error)) {
        std::cerr << error << "\n";
        return 2;
    }

    if (options.dryRun) {
        if (!dryRunProcessors(inputDb, options, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        return 0;
    }

    if (options.outputPath.empty()) {
        std::cerr << "--output is required when writing processors\n";
        return 2;
    }

    if (!runProcessors(options, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    if (options.printSummary) {
        std::cout << "Wrote processed log " << options.outputPath << "\n";
    }
    return 0;
}
