/**
 * @file    catalog_check.cc
 * @brief   Offline assertion checks for sensorviz::openLog() and StreamCatalog, without Qt.
 *
 * @details Opens the committed sensorViz fixture logs and checks what the
 *          catalog says about them: tag type and profile, time domain,
 *          collection start, record sets, the compass calibration, and the
 *          IMUTag magnetometer and accelerometer corrections, sample for
 *          sample against the stored rows. Then it edits a temporary copy of
 *          the IMUTag fixture to reach the paths no fixture does: resync
 *          events, and a newest calibration row that is empty, not JSON, or
 *          has no magnetometer.
 *
 *          Exits 0 when every check passes, 1 otherwise; each failure is
 *          printed with its line.
 */
#include "stream_catalog.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using sensorviz::StreamCatalog;
using sensorviz::StreamMeta;

int g_failures = 0; ///< Checks failed so far.
int g_checks = 0;   ///< Checks run so far.

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                              \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(condition)) {                                                           \
            g_failures++;                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                             \
    } while (0)

/// The committed fixture log @p name, e.g. `imutag.db3`.
std::string fixture(const char *name)
{
    return std::string(TAG_DESIGNS_SOURCE_DIR) + "/host/docs/fixtures/sensorviz/" + name;
}

/// The catalog's stream @p id, or null.
const StreamMeta *stream(const StreamCatalog &catalog, const char *id)
{
    for (const StreamMeta &meta : catalog.streams) {
        if (meta.id == id) {
            return &meta;
        }
    }
    return nullptr;
}

/// True when the two floats have the same bits.
bool sameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

/**
 * @brief   Checks IMUTag streams @p ids against @p table's rows, corrected by @p correct.
 *
 * @details Reads the stored rows as doubles with forEachRow(), applies
 *          @p correct to their floats as the loader does, and compares each
 *          with the stream's envelope at one column per sample, which holds
 *          every sample of these evenly spaced streams.
 */
template <typename Correct>
void checkCorrected(const StreamCatalog &catalog, const char *table,
                    const std::vector<const char *> &columns, const std::vector<const char *> &ids,
                    Correct correct)
{
    std::vector<std::vector<float>> expected(ids.size());
    std::vector<std::string> names(columns.begin(), columns.end());
    std::string error;
    const bool read = catalog.data->forEachRow(table, names, [&](double, const double *v) {
        const float raw[3] = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
        for (size_t axis = 0; axis < ids.size(); axis++) {
            expected[axis].push_back(correct(raw, static_cast<int>(axis)));
        }
    }, error);
    CHECK(read);
    for (size_t axis = 0; axis < ids.size(); axis++) {
        const StreamMeta *meta = stream(catalog, ids[axis]);
        CHECK(meta != nullptr);
        if (!meta) {
            continue;
        }
        const sensordata::StreamInfo *info = catalog.data->info(meta->dataId);
        CHECK(info != nullptr && info->samples == expected[axis].size());
        const sensordata::Envelope envelope = catalog.data->view(
            meta->dataId, -INFINITY, INFINITY, static_cast<int>(expected[axis].size()),
            sensordata::GapMode::Join);
        size_t wrong = 0;
        for (size_t i = 0; i < envelope.index.size(); i++) {
            if (!sameBits(envelope.value[i], expected[axis][envelope.index[i]])) {
                wrong++;
            }
        }
        CHECK(!envelope.index.empty());
        CHECK(wrong == 0);
    }
}

/// The IMUTag fixture: elapsed time, header start, calibrated magnetometer.
void checkImuTag()
{
    StreamCatalog catalog;
    std::string error;
    CHECK(sensorviz::openLog(fixture("imutag.db3"), catalog, error));
    CHECK(catalog.path == fixture("imutag.db3"));
    CHECK(catalog.tagType == "IMUTAG");
    CHECK(catalog.profileName == "IMUTAG");
    CHECK(catalog.info.count("uuid") == 1 && catalog.info.count("config") == 1);
    CHECK(catalog.timeDomain == sensordata::TimeDomain::ElapsedSeconds);
    CHECK(catalog.hasCollectionStart);
    CHECK(catalog.recordSets.empty());
    CHECK(catalog.eventMarkers.empty());
    CHECK(stream(catalog, "imu_pressure") != nullptr);

    CHECK(catalog.hasCompassCalibration);
    CHECK(catalog.compassCalibrationEpoch == 1786024933);
    CHECK(catalog.compassCalibrationWarning.empty());
    CHECK(catalog.compassCalibration.hardIron.x == 23.41959f);

    const compasscore::CalibrationConstants calibration = catalog.compassCalibration;
    checkCorrected(catalog, "ImuMag", {"mx", "my", "mz"}, {"imu_mx", "imu_my", "imu_mz"},
                   [&](const float *raw, int axis) {
                       const sensormath::Vec3f m = compasscore::applyMagnetometer(
                           calibration, sensormath::Vec3f{raw[0], raw[1], raw[2]});
                       return axis == 0 ? m.x : axis == 1 ? m.y : m.z;
                   });
    // No accelerometer section, so a zero offset: the axes are as stored.
    checkCorrected(catalog, "ImuAccel", {"ax", "ay", "az"}, {"imu_ax", "imu_ay", "imu_az"},
                   [](const float *raw, int axis) { return raw[axis]; });
}

/// The CompassTag fixture: epoch time, the compass_raw record set, calibration.
void checkCompassTag()
{
    StreamCatalog catalog;
    std::string error;
    CHECK(sensorviz::openLog(fixture("compasstag.db3"), catalog, error));
    CHECK(catalog.tagType == "COMPASSTAG");
    CHECK(catalog.profileName == "CompassTag");
    CHECK(catalog.timeDomain == sensordata::TimeDomain::EpochSeconds);
    CHECK(catalog.hasCollectionStart);
    CHECK(catalog.hasCompassCalibration);
    CHECK(stream(catalog, "imu_mx") == nullptr);

    const sensorviz::RecordSet *raw = nullptr;
    for (const sensorviz::RecordSet &set : catalog.recordSets) {
        if (set.id == "compass_raw") {
            raw = &set;
        }
    }
    CHECK(raw != nullptr);
    if (raw) {
        CHECK(raw->table == "Compass");
        CHECK(raw->timeDomain == sensordata::TimeDomain::EpochSeconds);
        CHECK((raw->columns == std::vector<std::string>{"ax", "ay", "az", "mx", "my", "mz"}));
        CHECK(catalog.data->info(raw->timeSource) != nullptr);
    }
    // The epoch start is the earliest epoch-time sample.
    double first = INFINITY;
    for (const sensordata::StreamInfo &info : catalog.data->streams()) {
        first = std::min(first, info.first_time);
    }
    CHECK(catalog.collectionStartEpochMs == static_cast<std::int64_t>(first * 1000.0));
}

/// Runs @p sql on the file @p path; true on success.
bool execute(const std::string &path, const char *sql)
{
    sqlite3 *db = nullptr;
    bool ok = sqlite3_open(path.c_str(), &db) == SQLITE_OK
        && sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

/// A fresh copy of the IMUTag fixture at @p path, edited by @p sql.
bool editedCopy(const std::string &path, const char *sql)
{
    std::error_code ec;
    std::filesystem::copy_file(fixture("imutag.db3"), path,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec && execute(path, sql);
}

/// Resync events, unusable calibrations, and a file that is not there.
void checkEdited()
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "sensorviz_catalog_check.db3").string();
    StreamCatalog catalog;
    std::string error;

    CHECK(editedCopy(path,
                     "INSERT INTO ImuEvent VALUES (2000000, 0, 'restart_recovery');"
                     "INSERT INTO ImuEvent VALUES (1500000, 0, 'OTHER');"
                     "INSERT INTO ImuEvent VALUES (1000000, 0, 'Resync_Late');"));
    CHECK(sensorviz::openLog(path, catalog, error));
    CHECK(catalog.eventMarkers.size() == 1);
    if (catalog.eventMarkers.size() == 1) {
        const sensorviz::EventMarker &marker = catalog.eventMarkers.front();
        CHECK(marker.id == "imu_resync");
        CHECK(marker.timeDomain == sensordata::TimeDomain::ElapsedSeconds);
        CHECK((marker.time == std::vector<double>{1.0, 2.0}));
    }

    const struct
    {
        const char *sql;      ///< Edit to the copy.
        const char *warning;  ///< Expected warning prefix.
    } unusable[] = {
        {"DELETE FROM Calibration;", "Calibration table is present but empty"},
        {"INSERT INTO Calibration VALUES (1999999999, 'not json');",
         "Compass calibration constants are not a valid JSON object"},
        {"INSERT INTO Calibration VALUES (1999999999, '{\"accelerometer\": {\"o0\": 1}}');",
         "Compass calibration constants do not contain a magnetometer object"},
    };
    for (const auto &edit : unusable) {
        CHECK(editedCopy(path, edit.sql));
        catalog = StreamCatalog();
        CHECK(sensorviz::openLog(path, catalog, error));
        CHECK(!catalog.hasCompassCalibration);
        CHECK(catalog.compassCalibrationWarning.rfind(edit.warning, 0) == 0);
        // Without a calibration the magnetometer is left as stored.
        checkCorrected(catalog, "ImuMag", {"mx", "my", "mz"}, {"imu_mx", "imu_my", "imu_mz"},
                       [](const float *raw, int axis) { return raw[axis]; });
    }

    // An accelerometer offset is subtracted from each axis.
    CHECK(editedCopy(path,
                     "INSERT INTO Calibration VALUES (1999999999, '{\"magnetometer\": {\"b\": 50},"
                     " \"accelerometer\": {\"o0\": 12.5, \"o1\": -3, \"o2\": 0.1}}');"));
    catalog = StreamCatalog();
    CHECK(sensorviz::openLog(path, catalog, error));
    CHECK(catalog.compassCalibrationEpoch == 1999999999);
    const sensormath::Vec3f offset = catalog.compassCalibration.accelOffset;
    CHECK(offset.x == 12.5f && offset.y == -3.0f && offset.z == 0.1f);
    checkCorrected(catalog, "ImuAccel", {"ax", "ay", "az"}, {"imu_ax", "imu_ay", "imu_az"},
                   [&](const float *raw, int axis) {
                       const float o = axis == 0 ? offset.x : axis == 1 ? offset.y : offset.z;
                       return static_cast<float>(static_cast<double>(raw[axis]) - o);
                   });
    std::filesystem::remove(path);

    error.clear();
    catalog = StreamCatalog();
    catalog.path = "unchanged";
    CHECK(!sensorviz::openLog(path + ".missing", catalog, error));
    CHECK(error.rfind("Could not open database: ", 0) == 0);
    CHECK(catalog.path == "unchanged");
}

} // namespace

/// Runs every check; exits 0 when all pass, 1 otherwise.
int main()
{
    checkImuTag();
    checkCompassTag();
    checkEdited();
    std::printf("catalog_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
