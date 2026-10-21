/**
 * @file    sensordata.h
 * @brief   Qt-free, decimating read access to a tagcore SQLite sensor log.
 *
 * @details A sensordata::Log reads every table named by a log's `streams`
 *          metadata once, into a compact columnar layout: one `double` time
 *          axis per table, shared by all of the table's columns, and one
 *          `float` vector per column. Each stream gets a min/max pyramid, and
 *          plots ask for an **M4 envelope** of a time window instead of the
 *          samples themselves: for every pixel column, the first, last,
 *          minimum and maximum sample. A polyline through the envelope
 *          rasterises to the same pixels as a polyline through every sample,
 *          and holds at most four points per column whatever the log's length.
 *
 *          The `float` copy exists only for drawing. Exact values come from
 *          SQLite: valueAt() reads the selected sample back as the stored
 *          `double`, and forEachRow() streams `double` rows to computations
 *          that must stay bit-identical to reading the database directly.
 *
 *          The library depends only on the sqlite3 C API, so command-line
 *          tools and non-Qt front ends can use it as well as sensorviz.
 *
 * @see     host/libraries/sensordata/README.md,
 *          host/applications/sensorviz/design/proposals/decimating-backend.md
 */
#ifndef SENSORDATA_SENSORDATA_H
#define SENSORDATA_SENSORDATA_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace sensordata {

/**
 * @enum    TimeDomain
 * @brief   What a stream's time coordinate counts from.
 *
 * @details Fixed by the table's time column name, as tagcore writes it: a
 *          column whose name ends in `ElapsedUs` (any case) holds
 *          microseconds from the start of collection and is loaded as
 *          seconds; every other time column holds whole epoch seconds.
 */
enum class TimeDomain {
    EpochSeconds,   ///< Seconds since the Unix epoch.
    ElapsedSeconds, ///< Seconds since the start of collection (IMUTag).
};

/**
 * @brief   Handle naming one stream of a Log.
 *
 * @details Handles are indices assigned in load order, then in addDerived()
 *          order. A handle is never reused within one Log, even after
 *          remove(), so a stale handle is detected rather than aliased.
 */
using StreamId = std::uint32_t;

/// A StreamId that names no stream.
constexpr StreamId kNoStream = std::numeric_limits<StreamId>::max();

/**
 * @struct  StreamInfo
 * @brief   Description and summary of one stream.
 *
 * @details For a stored stream the text fields come from the log's `streams`
 *          table. For a derived stream they are whatever the caller passed to
 *          addDerived(); the summary fields are filled in by the Log.
 */
struct StreamInfo {
    StreamId handle = kNoStream; ///< This stream's handle; set by the Log.
    std::string id;           ///< Stable stream id, `streams.stream_id`, e.g. `imu_ax`.
    std::string label;        ///< User-facing name, `streams.display_name`.
    std::string units;        ///< Units, possibly empty.
    std::string kind;         ///< `scalar` or `record_column` for stored streams; free text for derived ones.
    std::string group_id;     ///< `streams.group_id`, groups the columns of a record set; may be empty.
    std::string group_name;   ///< `streams.group_name`; may be empty.
    std::string table;        ///< SQLite table holding the samples; empty for a derived stream with its own time axis.
    std::string time_column;  ///< Time column of #table; empty when #table is.
    std::string value_column; ///< SQLite value column; empty for a derived stream.
    TimeDomain domain = TimeDomain::EpochSeconds; ///< Domain of the time axis.
    std::size_t samples = 0;  ///< Number of samples; never zero for a stream in the Log.
    double first_time = 0.0;  ///< Time of the first sample, seconds in #domain.
    double last_time = 0.0;   ///< Time of the last sample, seconds in #domain.
    float min = std::numeric_limits<float>::quiet_NaN(); ///< Smallest non-NaN value as stored (`float`); NaN if every sample is NaN.
    float max = std::numeric_limits<float>::quiet_NaN(); ///< Largest non-NaN value as stored (`float`); NaN if every sample is NaN.
    bool derived = false;     ///< True for a stream registered with addDerived().
};

/**
 * @struct  Envelope
 * @brief   The points to draw for one stream over one window, in time order.
 *
 * @details The three vectors are parallel. Indices are strictly increasing,
 *          so times are non-decreasing. Values are the stored `float`s; use
 *          valueAt() for an exact readout.
 */
struct Envelope {
    std::vector<double> time;        ///< Sample times, seconds in the stream's domain.
    std::vector<float> value;        ///< Sample values as stored.
    std::vector<std::size_t> index;  ///< Sample index of each point within the stream.
};

/**
 * @struct  Sample
 * @brief   One sample of a stream, as returned by Log::valueAt().
 */
struct Sample {
    std::size_t index = 0; ///< Sample index within the stream.
    double time = 0.0;     ///< Sample time, seconds in the stream's domain.
    double value = 0.0;    ///< Sample value; see #exact.
    bool exact = false;    ///< True when #value is the `double` read back from SQLite; false when it is the stored `float` widened (derived streams, or a failed read-back).
};

/**
 * @enum    Pick
 * @brief   Which sample Log::valueAt() selects for a time.
 */
enum class Pick {
    Nearest,   ///< The sample closest in time; on a tie, the earlier one.
    AtOrAfter, ///< The first sample at or after the time, or the last sample when every sample is earlier.
};

/**
 * @class   Log
 * @brief   One SQLite sensor log, loaded for decimated plotting and exact readout.
 *
 * @details A Log is created by open() and owns a read-only connection to the
 *          database for the rest of its life, which valueAt() uses one call
 *          at a time; forEachRow() opens its own. It never writes to the file.
 *
 *          **Loading.** Every metadata row of kind `scalar` or
 *          `record_column` becomes a stream. The rows naming the same table
 *          and time column are read together, with one query in rowid order.
 *          The writer inserts rows in sample order, so this is normally
 *          already time order; the loader checks, and a table found out of
 *          order is sorted once in memory by time (stably, so equal times keep
 *          their rowid order). A table with no rows contributes no streams.
 *
 *          **Threads.** view(), valueAt(), range(), forEachRow(), streams(),
 *          info() and find() are const and may be called concurrently from any
 *          number of threads. addDerived() and remove() change the stream
 *          set: the caller must ensure nothing else is using the Log while
 *          they run.
 *
 *          A Log is neither copyable nor movable; hold it through the
 *          `std::unique_ptr` that open() returns.
 */
class Log {
public:
    /**
     * @brief   Opens a log and loads every stream named by its metadata.
     *
     * @param[in]  path      Path of the SQLite file. It is opened read-only.
     * @param[out] error     Set to a user-readable message on failure; left
     *                       untouched on success.
     * @param[in]  progress  Optional. Called on the calling thread with the
     *                       fraction of rows read so far, from 0 to 1,
     *                       several times per table and once with 1 at the
     *                       end. May be empty.
     *
     * @return  The loaded log, or null if the file cannot be opened, has no
     *          `streams` table or no rows in it, has a metadata row missing a
     *          required field or with an unknown kind, or names a table or
     *          column that does not exist. A log whose tables are all empty
     *          loads successfully with no streams.
     *
     * @warning Blocking: reads every sample of the log. On a full 2 Gbit
     *          IMUTag log this takes several seconds; call it off any UI
     *          thread.
     */
    static std::unique_ptr<Log> open(const std::string &path,
                                     std::string &error,
                                     const std::function<void(double)> &progress = {});

    ~Log();
    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;

    /// Path the log was opened from.
    const std::string &path() const;

    /**
     * @brief   Lists the streams currently in the log.
     * @return  One entry per stream, stored streams first in metadata order,
     *          then derived streams in registration order. Removed streams
     *          are omitted.
     */
    std::vector<StreamInfo> streams() const;

    /**
     * @brief   Describes one stream.
     * @param[in] stream  Handle to look up.
     * @return  The stream's description, or null if @p stream names no
     *          stream (never assigned, or removed). The pointer is valid until
     *          the next addDerived() or remove().
     */
    const StreamInfo *info(StreamId stream) const;

    /**
     * @brief   Finds a stream by its stable id.
     * @param[in] id  Stream id, e.g. `imu_ax`.
     * @return  The handle of the most recently added stream with that id, so a
     *          derived stream registered under a stored stream's id is found
     *          in preference to it; nullopt if there is none.
     */
    std::optional<StreamId> find(const std::string &id) const;

    /**
     * @brief   Computes the M4 envelope of a stream over a time window.
     *
     * @details The window `[t0, t1]` is split into @p columns equal time
     *          intervals; column `c` holds the samples with time in
     *          `[t0 + (t1 - t0) * c / columns, t0 + (t1 - t0) * (c + 1) / columns)`,
     *          and the last column also holds samples at exactly @p t1.
     *          Because columns are split by time, not by sample count, gaps
     *          in the data stay where they are.
     *
     *          For each non-empty column the envelope holds its first sample,
     *          its last sample, and the first sample of its minimum and of its
     *          maximum value (NaN samples are ignored for the extremes), each
     *          once and in index order. It also holds the last sample before
     *          @p t0 and the first after @p t1, when they exist, so a line drawn
     *          through the envelope runs off both edges of the window rather
     *          than stopping at the last sample inside it.
     *
     *          Extremes come from the stream's min/max pyramid, so the cost
     *          depends on @p columns and the log's depth, not on the number of
     *          samples in the window. A column of 64 samples or fewer is
     *          scanned directly.
     *
     * @param[in] stream   Stream to draw.
     * @param[in] t0       Start of the window, seconds in the stream's domain.
     *                     -infinity means the first sample.
     * @param[in] t1       End of the window; +infinity means the last sample.
     * @param[in] columns  Number of columns, normally the plot's width in
     *                     device pixels.
     *
     * @return  At most `4 * columns + 2` points. Empty when @p stream names no
     *          stream, @p columns is not positive, either bound is NaN, or
     *          @p t1 is less than @p t0.
     */
    Envelope view(StreamId stream, double t0, double t1, int columns) const;

    /**
     * @brief   Selects one sample by time and reads its exact value.
     *
     * @details The sample is chosen from the time axis by binary search
     *          (see Pick). For a stored stream its value is then read back
     *          from SQLite by rowid, so it is exactly the `double` that
     *          `sqlite3_column_double()` returns for that row. That costs one
     *          indexed lookup, a few microseconds.
     *
     * @param[in] stream  Stream to read.
     * @param[in] t       Time, seconds in the stream's domain. NaN selects
     *                    nothing.
     * @param[in] pick    Which sample to select.
     *
     * @return  The selected sample, or nullopt when @p stream names no stream
     *          or @p t is NaN. For a derived stream, or if the read-back
     *          fails (for example because the file was removed), the value is
     *          the stored `float` and Sample::exact is false.
     */
    std::optional<Sample> valueAt(StreamId stream, double t, Pick pick = Pick::Nearest) const;

    /**
     * @brief   Smallest and largest value of a stream within a time window.
     *
     * @details Covers the samples with time in `[t0, t1]`, ignoring NaN, using
     *          the pyramid. Intended for automatic y-axis ranges. Pass
     *          infinite bounds for the whole stream, which is also what
     *          StreamInfo::min and StreamInfo::max hold.
     *
     * @param[in] stream  Stream to measure.
     * @param[in] t0      Start of the window, seconds; may be -infinity.
     * @param[in] t1      End of the window, seconds; may be +infinity.
     *
     * @return  `{min, max}` as stored (`float`), or nullopt when @p stream
     *          names no stream or the window holds no non-NaN sample.
     */
    std::optional<std::pair<float, float>> range(StreamId stream, double t0, double t1) const;

    /**
     * @brief   Streams `double` rows of a loaded table straight from SQLite.
     *
     * @details For computations that must see exactly the values in the
     *          database, such as sensoranalysis inputs, rather than the `float`
     *          copy kept for drawing. Rows are delivered in the order of the
     *          table's loaded time axis, with that axis's time, so row `i`
     *          corresponds to sample index `i` of every stream of the table.
     *          Values are read with `sqlite3_column_double()`, so NULL reads
     *          as 0.
     *
     *          A table found out of order at load time is read in rowid
     *          order into a temporary buffer of the requested columns and
     *          delivered in time order from there.
     *
     *          The pass uses a read-only connection of its own, opened on
     *          path(), so it does not hold up valueAt() on other threads.
     *
     * @param[in]  table    SQLite table, as named in StreamInfo::table. It
     *                      must hold at least one loaded stream.
     * @param[in]  columns  Columns to read, any columns of the table.
     * @param[in]  fn       Called once per row with the row's time (seconds,
     *                      the same `double` as the time axis) and a pointer to
     *                      `columns.size()` values.
     * @param[out] error    Set when the function returns false.
     *
     * @return  true when every row was delivered. false if no loaded stream
     *          uses @p table, the file cannot be reopened, a column does not
     *          exist, or the table's rows no longer match what was loaded;
     *          @p fn may already have been called for some rows.
     *
     * @warning Blocking: a full pass over the table, about 1.8 s for a full
     *          IMUTag table.
     */
    bool forEachRow(const std::string &table,
                    const std::vector<std::string> &columns,
                    const std::function<void(double time, const double *values)> &fn,
                    std::string &error) const;

    /**
     * @brief   Registers a computed stream on the time axis of an existing stream.
     *
     * @details The values are narrowed to `float` for storage and a pyramid is
     *          built; the `double`s are not kept. The new stream shares
     *          @p time_from's time axis, so it costs no time storage, and
     *          survives remove() of @p time_from.
     *
     * @param[in] info       Description. Its text fields are kept; handle,
     *                       table, time_column, domain, samples, the time and
     *                       value summaries and `derived` are filled in.
     *                       value_column is cleared.
     * @param[in] time_from  Stream whose time axis the values follow.
     * @param[in] values     One value per sample of @p time_from.
     *
     * @return  The new stream's handle, or nullopt when @p time_from names no
     *          stream or @p values has the wrong length.
     *
     * @warning Not thread-safe; see the class description.
     */
    std::optional<StreamId> addDerived(StreamInfo info, StreamId time_from,
                                       const std::vector<double> &values);

    /**
     * @brief   As the overload above, reading @p count values from @p values.
     *
     * @details For callers whose values are not in a `std::vector`, such as a
     *          Qt container, so they need not make a temporary copy.
     *
     * @param[in] info       Description, as above.
     * @param[in] time_from  Stream whose time axis the values follow.
     * @param[in] values     @p count values; may be null only when @p count is
     *                       zero, which is then rejected.
     * @param[in] count      Number of values; must equal @p time_from's sample
     *                       count.
     * @return  As above.
     *
     * @warning Not thread-safe; see the class description.
     */
    std::optional<StreamId> addDerived(StreamInfo info, StreamId time_from, const double *values,
                                       std::size_t count);

    /**
     * @brief   Registers a stream computed sample by sample from other streams' stored values.
     *
     * @details For display transforms that may work from the `float` copy,
     *          such as a vector magnitude: no `double`s are read from SQLite
     *          or held, so the only memory used is the result's. @p fn is called
     *          once per sample, in sample order, with the inputs' stored
     *          `float` values for that sample, in the order of @p inputs; its
     *          result is narrowed to `float`. The new stream shares the
     *          inputs' time axis.
     *
     *          Results differ from the same arithmetic on the SQLite `double`s
     *          by the `float` rounding of the inputs, about one part in 10^7.
     *          Computations that must match the database bit for bit use
     *          forEachRow() and the other overloads instead.
     *
     * @param[in] info    Description, as for addDerived().
     * @param[in] inputs  At least one stream, all on the same time axis (the
     *                    columns of one table, or streams derived on it).
     * @param[in] fn      Maps one sample's input values to the output value.
     *
     * @return  The new stream's handle, or nullopt when @p inputs is empty,
     *          names a stream that does not exist, or mixes time axes, or
     *          @p fn is empty.
     *
     * @warning Not thread-safe; see the class description. Blocking: one pass
     *          over the inputs.
     */
    std::optional<StreamId> addComputed(StreamInfo info, const std::vector<StreamId> &inputs,
                                        const std::function<double(const float *values)> &fn);

    /**
     * @brief   Registers a computed stream with its own time axis.
     *
     * @param[in] info    Description; as for the other overload, except that
     *                    `domain` is taken from @p info and table and
     *                    time_column are cleared.
     * @param[in] time    Sample times, seconds, non-decreasing.
     * @param[in] values  One value per time.
     *
     * @return  The new stream's handle, or nullopt when the vectors are empty,
     *          differ in length, or @p time decreases or holds NaN.
     *
     * @warning Not thread-safe; see the class description.
     */
    std::optional<StreamId> addDerived(StreamInfo info, std::vector<double> time,
                                       const std::vector<double> &values);

    /**
     * @brief   Removes a stream and frees its values and pyramid.
     *
     * @details Works on stored and derived streams alike. A time axis is
     *          freed when no remaining stream uses it; a stored table's axis
     *          stays available to forEachRow() as long as any stream uses it.
     *
     * @param[in] stream  Stream to remove.
     * @return  false if @p stream names no stream.
     *
     * @warning Not thread-safe; see the class description.
     */
    bool remove(StreamId stream);

private:
    struct Impl;
    explicit Log(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace sensordata

#endif // SENSORDATA_SENSORDATA_H
