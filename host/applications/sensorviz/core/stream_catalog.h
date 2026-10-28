/**
 * @file    stream_catalog.h
 * @brief   Qt-free description of an opened tag log: its streams, record sets,
 *          events, info rows and calibration, and the function that reads it.
 *
 * @details Part of `sensorviz_core`. openLog() is the only place sensorViz
 *          reads a log's SQLite file outside the `sensordata` backend: it
 *          opens the samples with sensordata::Log::open(), then reads what
 *          sits beside them -- the `info` table, the IMUTag header and
 *          events, and the newest compass calibration -- and assembles a
 *          StreamCatalog. Front ends work with the catalog and never issue
 *          SQL themselves.
 *
 *          Strings are UTF-8, as SQLite stores them; a front end converts at
 *          its own edge.
 *
 * @see     stream_meta.h, view_model.h,
 *          host/applications/sensorviz/design/proposals/toolkit-neutral-core.md
 */
#ifndef SENSORVIZ_CORE_STREAM_CATALOG_H
#define SENSORVIZ_CORE_STREAM_CATALOG_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "compass_core.h"
#include "sensordata.h"
#include "stream_meta.h"

namespace sensorviz {

/**
 * @struct  RecordSet
 * @brief   A multi-column table that is not itself a plottable stream.
 *
 * @details Compass data is the motivating case: one Compass table holds
 *          accelerometer and magnetometer columns, which the compass
 *          transform turns into heading, pitch, roll and the rest. A record
 *          set is the `streams` metadata's `record_column` rows of one group.
 */
struct RecordSet
{
    std::string id;    ///< Stable id used by transform code, the rows' group id, e.g. `compass_raw`.
    std::string label; ///< User-facing name (the group name, else the id), shown in File Info.
    std::string table; ///< SQLite table holding the columns, all on one time axis.
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds; ///< Domain of the table's times.
    /// Column names, in metadata order, as `value_column` gives them. Transform
    /// code owns their meaning and reads them as doubles with
    /// sensordata::Log::forEachRow().
    std::vector<std::string> columns;
    /// A stored stream on this table's time axis, so a stream derived row by
    /// row from the record set can share that axis.
    sensordata::StreamId timeSource = sensordata::kNoStream;
};

/**
 * @struct  EventMarker
 * @brief   Discrete log events, drawn as vertical lines; they own no y axis.
 */
struct EventMarker
{
    std::string id;           ///< Stable id, e.g. `imu_resync`.
    std::string label;        ///< User-facing name.
    std::vector<double> time; ///< Event times, seconds in @ref timeDomain, ascending.
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds; ///< Domain of @ref time.
};

/**
 * @struct  StreamCatalog
 * @brief   Everything sensorViz knows about one opened log, above its samples.
 *
 * @details Filled by openLog() and not changed by it afterwards. Transforms
 *          add their derived streams to the ViewModel, not here. @ref data is
 *          shared so that a catalog can be copied between a loader thread and
 *          the front end; there is one sensordata::Log per opened file, and
 *          the stored streams' StreamMeta::dataId values name streams in it.
 *
 *          Feature availability follows the streams and record sets present,
 *          never @ref tagType, which is metadata for display and preference
 *          keys.
 */
struct StreamCatalog
{
    std::string path;        ///< The file as opened, UTF-8; empty when no log is loaded.
    std::string tagType;     ///< The `info` table's `tagtype` value; may be empty.
    std::string profileName; ///< A display name for the tag family, from @ref tagType ("Sensor log" when unknown).

    /// The `info` table, by field name. A repeated field keeps its last
    /// value. Large blobs (`config`, `info`) are kept; File Info skips them.
    std::map<std::string, std::string> info;

    /// True when the log gives an absolute start of collection: the first
    /// ImuHeader row for IMUTag, else the earliest epoch-time sample.
    bool hasCollectionStart = false;
    std::int64_t collectionStartEpochMs = 0; ///< That start, ms since the Unix epoch.

    std::vector<StreamMeta> streams;        ///< Stored scalar streams, in metadata order, with display defaults.
    std::vector<RecordSet> recordSets;      ///< Record sets, ordered by id.
    std::vector<EventMarker> eventMarkers;  ///< Event markers; only those with at least one event.

    /// True when a usable compass calibration was read from the newest
    /// Calibration row.
    bool hasCompassCalibration = false;
    std::int64_t compassCalibrationEpoch = 0; ///< That row's Epoch, s since the Unix epoch.
    compasscore::CalibrationConstants compassCalibration; ///< The calibration; identity when absent.
    /// Why the calibration is absent or suspect; empty when there is nothing to say.
    std::string compassCalibrationWarning;

    /// ElapsedSeconds when any stream or record set is on elapsed time
    /// (IMUTag), else EpochSeconds.
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds;

    /// The log's samples: float copies, min/max pyramids and exact read-back.
    std::shared_ptr<sensordata::Log> data;
};

/**
 * @brief   Opens a tag SQLite log read-only and describes it.
 *
 * @details Reads the samples through sensordata::Log::open(), which
 *          validates the mandatory `streams` metadata table; then the
 *          `info` table, the IMUTag collection start and resync events, and
 *          the newest compass calibration (through
 *          compasscore::calibrationFromJson()). When a calibration is
 *          present, IMUTag's stored magnetometer axes are replaced with
 *          calibrated ones (compasscore::applyMagnetometer() on the stored
 *          floats) and the accelerometer zero-g offset is subtracted from
 *          its acceleration axes; CompassTag's raw rows are left for the
 *          compass transform, which calibrates them itself.
 *
 *          A calibration that cannot be used is not an error: it leaves
 *          StreamCatalog::hasCompassCalibration false and says why in
 *          StreamCatalog::compassCalibrationWarning.
 *
 * @param[in]  path     The log's path, UTF-8.
 * @param[out] catalog  The description; replaced only on success.
 * @param[out] error    Why the log could not be opened; set only on failure.
 *
 * @return  true on success. false when the file cannot be opened, its
 *          `streams` metadata is missing or malformed, an IMUTag header,
 *          event or calibration query fails, or the log has no streams and
 *          no record sets.
 *
 * @note    Blocks while it reads every sample; front ends call it off their
 *          UI thread.
 */
bool openLog(const std::string &path, StreamCatalog &catalog, std::string &error);

} // namespace sensorviz

#endif
