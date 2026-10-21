/**
 * @file    sensordata.cc
 * @brief   Loading, M4 views and exact readout for sensordata::Log.
 *
 * @details The loader reads the `streams` metadata, groups its rows by table
 *          and time column, and reads each group with one
 *          `SELECT rowid, time, columns... ORDER BY rowid`. Rowids are kept
 *          only when they are not contiguous, so valueAt() can map a sample
 *          index back to its row. See sensordata.h for the contracts.
 */
#include "sensordata.h"

#include "minmax_pyramid.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>

namespace sensordata {

namespace {

using detail::MinMaxPyramid;

/// Columns of 64 samples or fewer are scanned directly; below that the pyramid walk costs more than it saves.
constexpr std::size_t kDirectScanSamples = 64;

/// Rows read between progress callbacks.
constexpr std::size_t kProgressRows = 1u << 16;

// ---------------------------------------------------------------------------
// SQLite helpers
// ---------------------------------------------------------------------------

/**
 * @brief   Owns one prepared statement; finalizes it on destruction.
 */
class Statement {
public:
    /// Prepares @p sql on @p db; check valid() before use.
    Statement(sqlite3 *db, const std::string &sql) : db_(db)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            error_ = sqlite3_errmsg(db);
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

    /// True when the statement prepared.
    bool valid() const { return stmt_ != nullptr; }

    /// Steps once; true on a row. A failure other than SQLITE_DONE is kept for error().
    bool next()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            error_ = sqlite3_errmsg(db_);
        }
        return false;
    }

    /// True if a step failed (rather than finishing).
    bool failed() const { return !error_.empty(); }

    /// The prepare or step error message.
    const std::string &error() const { return error_; }

    /// The raw statement handle, for column and bind calls.
    sqlite3_stmt *get() const { return stmt_; }

    /// Column @p column as UTF-8 text; NULL reads as an empty string.
    std::string text(int column) const
    {
        const unsigned char *value = sqlite3_column_text(stmt_, column);
        return value ? reinterpret_cast<const char *>(value) : "";
    }

private:
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    std::string error_;
};

/// @p identifier as a double-quoted SQL identifier.
std::string quoted(const std::string &identifier)
{
    std::string out = "\"";
    for (char c : identifier) {
        out += c;
        if (c == '"') {
            out += '"';
        }
    }
    out += '"';
    return out;
}

/// True when @p a and @p b are equal ignoring ASCII case.
bool equalsIgnoringCase(const std::string &a, const std::string &b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x))
                   == std::tolower(static_cast<unsigned char>(y));
           });
}

/// True when the database has a table named @p name, ignoring case as SQLite does.
bool tableExists(sqlite3 *db, const std::string &name)
{
    Statement stmt(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND lower(name)=lower(?)");
    if (!stmt.valid()) {
        return false;
    }
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    return stmt.next();
}

/**
 * @brief   Checks that every one of @p columns exists in @p table.
 *
 * @details Needed because SQLite reads a double-quoted name that matches no
 *          column as a string literal, so a query naming a missing column
 *          would succeed and return the name, which reads as 0.
 *
 * @param[in]  db       Connection.
 * @param[in]  table    Table to inspect.
 * @param[in]  columns  Column names, compared ignoring case as SQLite does.
 * @param[out] missing  The first missing column, when the result is false.
 * @return  true when all exist.
 */
bool columnsExist(sqlite3 *db, const std::string &table, const std::vector<std::string> &columns,
                  std::string &missing)
{
    std::vector<std::string> names;
    Statement stmt(db, "SELECT name FROM pragma_table_info(?)");
    if (stmt.valid()) {
        sqlite3_bind_text(stmt.get(), 1, table.c_str(), -1, SQLITE_TRANSIENT);
        while (stmt.next()) {
            names.push_back(stmt.text(0));
        }
    }
    for (const std::string &column : columns) {
        const bool found = std::any_of(names.begin(), names.end(), [&](const std::string &name) {
            return equalsIgnoringCase(name, column);
        });
        if (!found) {
            missing = column;
            return false;
        }
    }
    return true;
}

/**
 * @brief   Opens @p path read-only on a connection for one thread at a time.
 *
 * @details SQLITE_OPEN_NOMUTEX: in serialized mode every
 *          `sqlite3_column_*()` call takes the connection's mutex, which made
 *          loading a full IMUTag log about 1.6 times slower. Callers serialise
 *          use of the connection themselves.
 *
 * @param[in]  path   File to open.
 * @param[out] db     The connection, or null on failure.
 * @param[out] error  Set on failure.
 * @return  true on success.
 */
bool openReadOnly(const std::string &path, sqlite3 *&db, std::string &error)
{
    db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr)
        != SQLITE_OK) {
        error = std::string("Could not open database: ") + (db ? sqlite3_errmsg(db) : "out of memory");
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
        return false;
    }
    return true;
}

/**
 * @brief   Owns a connection; closes it on destruction.
 */
struct Connection {
    sqlite3 *db = nullptr; ///< The connection, or null.
    ~Connection()
    {
        if (db) {
            sqlite3_close(db);
        }
    }
};

/// The time domain tagcore implies by naming a time column.
TimeDomain domainForColumn(const std::string &column)
{
    static const std::string suffix = "elapsedus";
    if (column.size() >= suffix.size()
        && equalsIgnoringCase(column.substr(column.size() - suffix.size()), suffix)) {
        return TimeDomain::ElapsedSeconds;
    }
    return TimeDomain::EpochSeconds;
}

/// Converts a stored integer time to seconds in @p domain, exactly as sensorviz always has.
double secondsFromStored(sqlite3_int64 stored, TimeDomain domain)
{
    const double value = static_cast<double>(stored);
    return domain == TimeDomain::ElapsedSeconds ? value / 1000000.0 : value;
}

// ---------------------------------------------------------------------------
// In-memory layout
// ---------------------------------------------------------------------------

/**
 * @brief   One time axis: a loaded table's, or a derived stream's own.
 */
struct Axis {
    std::string table;        ///< SQLite table; empty for a derived axis.
    std::string time_column;  ///< Its time column; empty for a derived axis.
    TimeDomain domain = TimeDomain::EpochSeconds; ///< Domain of #time.
    std::vector<double> time; ///< Seconds, non-decreasing.
    sqlite3_int64 first_rowid = 0;        ///< Rowid of sample 0 when #rowids is empty.
    std::vector<sqlite3_int64> rowids;    ///< Rowid per sample; empty when rowids are #first_rowid + index.
    std::vector<std::size_t> load_position; ///< Position in rowid order per sample; empty when the table was already in time order.

    /// Rowid of sample @p index.
    sqlite3_int64 rowid(std::size_t index) const
    {
        return rowids.empty() ? first_rowid + static_cast<sqlite3_int64>(index) : rowids[index];
    }
};

/**
 * @brief   One stream: description, drawing values and their pyramid.
 */
struct StreamData {
    StreamInfo info;                   ///< Public description.
    std::shared_ptr<const Axis> axis;  ///< Time axis, shared with the table's other streams.
    std::vector<float> values;         ///< One per sample of #axis.
    MinMaxPyramid pyramid;             ///< Over #values.
};

/// Fills the time and value summaries of @p stream from its axis and values.
void finishStream(StreamData &stream)
{
    stream.pyramid.build(stream.values.data(), stream.values.size());
    const MinMaxPyramid::Extremes all =
        stream.pyramid.query(stream.values.data(), 0, stream.values.size(), false);
    StreamInfo &info = stream.info;
    info.samples = stream.values.size();
    info.first_time = stream.axis->time.front();
    info.last_time = stream.axis->time.back();
    info.domain = stream.axis->domain;
    info.min = all.valid ? all.min : std::nanf("");
    info.max = all.valid ? all.max : std::nanf("");
}

/**
 * @brief   First index at or after @p from whose time does not satisfy @p before.
 *
 * @details A galloping search: it probes outward from @p guess in doubling
 *          steps until the answer is bracketed, then bisects the bracket.
 *          For evenly sampled data and a good guess this reads a handful of
 *          neighbouring entries instead of the log2(n) scattered ones a
 *          plain binary search reads.
 *
 * @param[in] time    Non-decreasing times.
 * @param[in] from    Lower limit of the answer; every index below it is
 *                    known to satisfy @p before.
 * @param[in] guess   Where to start probing; clamped to `[from, size]`.
 * @param[in] bound   Value compared against.
 * @param[in] before  `before(time[i], bound)` is true for every index below
 *                    the answer and false from it on (`<` for a lower bound,
 *                    `<=` for an upper bound).
 * @return  The answer, in `[from, time.size()]`.
 */
template <typename Before>
std::size_t firstNotBefore(const std::vector<double> &time, std::size_t from, std::size_t guess,
                           double bound, Before before)
{
    const std::size_t n = time.size();
    guess = std::min(std::max(guess, from), n);
    std::size_t lo;
    std::size_t hi;
    if (guess < n && before(time[guess], bound)) {
        // The answer is above guess: gallop up.
        lo = guess + 1;
        std::size_t step = 1;
        hi = lo;
        while (hi < n && before(time[hi], bound)) {
            lo = hi + 1;
            hi = std::min(n, hi + step);
            step *= 2;
        }
    } else {
        // The answer is at or below guess: gallop down.
        hi = guess;
        std::size_t step = 1;
        lo = hi;
        while (lo > from && !before(time[lo - 1], bound)) {
            hi = lo - 1;
            lo = lo - 1 >= from + step ? lo - 1 - step : from;
            step *= 2;
        }
    }
    // Invariant: the answer is in [lo, hi].
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (before(time[mid], bound)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/// Sample index range `[begin, end)` with time in `[t0, t1]`.
std::pair<std::size_t, std::size_t> indexRange(const std::vector<double> &time, double t0,
                                               double t1)
{
    const auto begin = std::lower_bound(time.begin(), time.end(), t0);
    const auto end = std::upper_bound(begin, time.end(), t1);
    return {static_cast<std::size_t>(begin - time.begin()),
            static_cast<std::size_t>(end - time.begin())};
}

/// One row of the `streams` metadata table.
struct Metadata {
    std::string id;            ///< stream_id
    std::string group_id;      ///< group_id
    std::string group_name;    ///< group_name
    std::string table;         ///< table_name
    std::string time_column;   ///< time_column
    std::string value_column;  ///< value_column
    std::string kind;          ///< stream_kind
    std::string display_name;  ///< display_name
    std::string units;         ///< units
};

/// Reads and validates the `streams` table.
bool readMetadata(sqlite3 *db, std::vector<Metadata> &rows, std::string &error)
{
    if (!tableExists(db, "streams")) {
        error = "SQLite log does not contain required stream metadata";
        return false;
    }
    Statement stmt(db,
                   "SELECT stream_id, group_id, group_name, table_name, time_column, "
                   "value_column, stream_kind, display_name, units "
                   "FROM streams ORDER BY rowid");
    if (!stmt.valid()) {
        error = "Failed to load stream metadata: " + stmt.error();
        return false;
    }
    while (stmt.next()) {
        Metadata row;
        row.id = stmt.text(0);
        row.group_id = stmt.text(1);
        row.group_name = stmt.text(2);
        row.table = stmt.text(3);
        row.time_column = stmt.text(4);
        row.value_column = stmt.text(5);
        row.kind = stmt.text(6);
        row.display_name = stmt.text(7);
        row.units = stmt.text(8);
        if (row.id.empty() || row.table.empty() || row.time_column.empty()
            || row.value_column.empty() || row.kind.empty() || row.display_name.empty()) {
            error = "Stream metadata contains an incomplete row";
            return false;
        }
        if (equalsIgnoringCase(row.kind, "scalar")) {
            row.kind = "scalar";
        } else if (equalsIgnoringCase(row.kind, "record_column")) {
            row.kind = "record_column";
            if (row.group_id.empty()) {
                error = "Record column " + row.id + " does not specify a group";
                return false;
            }
        } else {
            error = "Unsupported stream kind " + row.kind + " for stream " + row.id;
            return false;
        }
        rows.push_back(std::move(row));
    }
    if (stmt.failed()) {
        error = "Failed to load stream metadata: " + stmt.error();
        return false;
    }
    if (rows.empty()) {
        error = "SQLite log contains no stream metadata rows";
        return false;
    }

    // A record set is one table read with one time column; mixing would make
    // its columns unequal in length, so refuse it as sensorviz always has.
    std::map<std::string, const Metadata *> record_sets;
    for (const Metadata &row : rows) {
        if (row.kind != "record_column") {
            continue;
        }
        auto inserted = record_sets.emplace(row.group_id, &row);
        const Metadata &first = *inserted.first->second;
        if (first.table != row.table || first.time_column != row.time_column) {
            error = "Record set " + row.group_id + " mixes tables or time columns";
            return false;
        }
    }
    return true;
}

/// Metadata rows sharing one table and time column, read with one query.
struct TableGroup {
    std::string table;                ///< Table name as the metadata spells it.
    std::string time_column;          ///< Time column.
    std::vector<std::size_t> rows;    ///< Indices into the metadata rows.
    std::size_t row_count = 0;        ///< `count(*)`, for reserving and progress.
};

} // namespace

// ---------------------------------------------------------------------------
// Log::Impl
// ---------------------------------------------------------------------------

/**
 * @brief   State behind Log: the connection and the streams.
 */
struct Log::Impl {
    std::string path;      ///< Path opened.
    sqlite3 *db = nullptr; ///< Read-only connection opened without SQLite's own mutexes, which cost a lock per column read.
    mutable std::mutex db_mutex; ///< Serialises valueAt() read-backs on #db after open().
    std::vector<std::unique_ptr<StreamData>> streams; ///< Indexed by StreamId; null once removed.

    ~Impl()
    {
        if (db) {
            sqlite3_close(db);
        }
    }

    /// The live stream @p id, or null.
    const StreamData *stream(StreamId id) const
    {
        return id < streams.size() ? streams[id].get() : nullptr;
    }

    /// Appends @p stream, assigning its handle.
    StreamId add(std::unique_ptr<StreamData> stream)
    {
        const StreamId id = static_cast<StreamId>(streams.size());
        stream->info.handle = id;
        streams.push_back(std::move(stream));
        return id;
    }

    bool loadGroup(const TableGroup &group, const std::vector<Metadata> &metadata,
                   std::shared_ptr<Axis> &axis, std::vector<std::vector<float>> &values,
                   std::size_t &rows_done, std::size_t rows_total,
                   const std::function<void(double)> &progress, std::string &error);
};

/**
 * @brief   Reads one table group: time axis, rowids and float value columns.
 *
 * @param[in]     group       Table, time column and metadata rows to read.
 * @param[in]     metadata    All metadata rows; @p group indexes into them.
 * @param[out]    axis        The loaded axis; time order, with rowids and
 *                            load positions as needed.
 * @param[out]    values      One vector per row of @p group, in that order.
 * @param[in,out] rows_done   Rows read so far over all groups, for progress.
 * @param[in]     rows_total  Rows to read over all groups.
 * @param[in]     progress    Optional progress callback.
 * @param[out]    error       Set on failure.
 * @return  false if the table is missing or the query fails.
 */
bool Log::Impl::loadGroup(const TableGroup &group, const std::vector<Metadata> &metadata,
                          std::shared_ptr<Axis> &axis, std::vector<std::vector<float>> &values,
                          std::size_t &rows_done, std::size_t rows_total,
                          const std::function<void(double)> &progress, std::string &error)
{
    axis = std::make_shared<Axis>();
    axis->table = group.table;
    axis->time_column = group.time_column;
    axis->domain = domainForColumn(group.time_column);

    std::vector<std::string> names = {group.time_column};
    std::string sql = "SELECT rowid, " + quoted(group.time_column);
    for (std::size_t row : group.rows) {
        names.push_back(metadata[row].value_column);
        sql += ", " + quoted(metadata[row].value_column);
    }
    std::string missing;
    if (!columnsExist(db, group.table, names, missing)) {
        error = "Stream metadata references missing column " + missing + " of table " + group.table;
        return false;
    }
    sql += " FROM " + quoted(group.table) + " ORDER BY rowid";
    Statement stmt(db, sql);
    if (!stmt.valid()) {
        error = "Failed to load " + group.table + ": " + stmt.error();
        return false;
    }

    const std::size_t columns = group.rows.size();
    values.assign(columns, {});
    axis->time.reserve(group.row_count);
    for (std::vector<float> &column : values) {
        column.reserve(group.row_count);
    }

    bool in_order = true;
    bool contiguous = true;
    sqlite3_stmt *raw = stmt.get();
    while (stmt.next()) {
        const sqlite3_int64 rowid = sqlite3_column_int64(raw, 0);
        const double time = secondsFromStored(sqlite3_column_int64(raw, 1), axis->domain);
        const std::size_t index = axis->time.size();
        if (index == 0) {
            axis->first_rowid = rowid;
        } else {
            if (time < axis->time.back()) {
                in_order = false;
            }
            if (contiguous && rowid != axis->first_rowid + static_cast<sqlite3_int64>(index)) {
                // First gap: materialise the rowids implied so far.
                contiguous = false;
                axis->rowids.reserve(group.row_count);
                for (std::size_t i = 0; i < index; i++) {
                    axis->rowids.push_back(axis->first_rowid + static_cast<sqlite3_int64>(i));
                }
            }
        }
        if (!contiguous) {
            axis->rowids.push_back(rowid);
        }
        axis->time.push_back(time);
        for (std::size_t c = 0; c < columns; c++) {
            values[c].push_back(static_cast<float>(sqlite3_column_double(raw, static_cast<int>(c + 2))));
        }
        if (++rows_done % kProgressRows == 0 && progress && rows_total > 0) {
            progress(static_cast<double>(rows_done) / static_cast<double>(rows_total));
        }
    }
    if (stmt.failed()) {
        error = "Failed to load " + group.table + ": " + stmt.error();
        return false;
    }

    if (!in_order) {
        // Restore time order once, stably, and remember where each sample
        // came from so forEachRow() can reproduce the order.
        const std::size_t count = axis->time.size();
        std::vector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), std::size_t(0));
        const std::vector<double> &time = axis->time;
        std::stable_sort(order.begin(), order.end(),
                         [&time](std::size_t a, std::size_t b) { return time[a] < time[b]; });

        std::vector<double> sorted_time(count);
        std::vector<sqlite3_int64> sorted_rowids(count);
        for (std::size_t i = 0; i < count; i++) {
            sorted_time[i] = time[order[i]];
            sorted_rowids[i] = axis->rowid(order[i]);
        }
        for (std::vector<float> &column : values) {
            std::vector<float> sorted(count);
            for (std::size_t i = 0; i < count; i++) {
                sorted[i] = column[order[i]];
            }
            column.swap(sorted);
        }
        axis->time.swap(sorted_time);
        axis->rowids.swap(sorted_rowids);
        axis->load_position.swap(order);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

Log::Log(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Log::~Log() = default;

std::unique_ptr<Log> Log::open(const std::string &path, std::string &error,
                               const std::function<void(double)> &progress)
{
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    if (!openReadOnly(path, impl->db, error)) {
        return nullptr;
    }

    std::vector<Metadata> metadata;
    if (!readMetadata(impl->db, metadata, error)) {
        return nullptr;
    }

    // Group rows by table and time column, in order of first appearance.
    std::vector<TableGroup> groups;
    std::vector<std::size_t> group_of(metadata.size());
    for (std::size_t i = 0; i < metadata.size(); i++) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const TableGroup &group) {
            return group.table == metadata[i].table
                && group.time_column == metadata[i].time_column;
        });
        if (it == groups.end()) {
            groups.push_back(TableGroup{metadata[i].table, metadata[i].time_column, {}, 0});
            it = groups.end() - 1;
        }
        group_of[i] = static_cast<std::size_t>(it - groups.begin());
        it->rows.push_back(i);
    }

    std::size_t rows_total = 0;
    for (TableGroup &group : groups) {
        if (!tableExists(impl->db, group.table)) {
            error = "Stream metadata references missing table " + group.table;
            return nullptr;
        }
        Statement count(impl->db, "SELECT count(*) FROM " + quoted(group.table));
        if (count.valid() && count.next()) {
            group.row_count = static_cast<std::size_t>(sqlite3_column_int64(count.get(), 0));
        }
        rows_total += group.row_count;
    }

    // Load each group, then hand out streams in metadata order.
    std::vector<std::shared_ptr<Axis>> axes(groups.size());
    std::vector<std::vector<std::vector<float>>> group_values(groups.size());
    std::size_t rows_done = 0;
    for (std::size_t g = 0; g < groups.size(); g++) {
        if (!impl->loadGroup(groups[g], metadata, axes[g], group_values[g], rows_done,
                             rows_total, progress, error)) {
            return nullptr;
        }
    }

    for (std::size_t i = 0; i < metadata.size(); i++) {
        const std::size_t g = group_of[i];
        if (axes[g]->time.empty()) {
            continue; // Empty tables contribute no streams.
        }
        const auto column = std::find(groups[g].rows.begin(), groups[g].rows.end(), i)
            - groups[g].rows.begin();
        const Metadata &row = metadata[i];
        auto stream = std::make_unique<StreamData>();
        stream->info.id = row.id;
        stream->info.label = row.display_name;
        stream->info.units = row.units;
        stream->info.kind = row.kind;
        stream->info.group_id = row.group_id;
        stream->info.group_name = row.group_name;
        stream->info.table = row.table;
        stream->info.time_column = row.time_column;
        stream->info.value_column = row.value_column;
        stream->axis = axes[g];
        stream->values.swap(group_values[g][static_cast<std::size_t>(column)]);
        finishStream(*stream);
        impl->add(std::move(stream));
    }

    if (progress) {
        progress(1.0);
    }
    return std::unique_ptr<Log>(new Log(std::move(impl)));
}

const std::string &Log::path() const
{
    return impl_->path;
}

std::vector<StreamInfo> Log::streams() const
{
    std::vector<StreamInfo> out;
    for (const std::unique_ptr<StreamData> &stream : impl_->streams) {
        if (stream) {
            out.push_back(stream->info);
        }
    }
    return out;
}

const StreamInfo *Log::info(StreamId stream) const
{
    const StreamData *data = impl_->stream(stream);
    return data ? &data->info : nullptr;
}

std::optional<StreamId> Log::find(const std::string &id) const
{
    for (std::size_t i = impl_->streams.size(); i-- > 0;) {
        if (impl_->streams[i] && impl_->streams[i]->info.id == id) {
            return static_cast<StreamId>(i);
        }
    }
    return std::nullopt;
}

Envelope Log::view(StreamId stream, double t0, double t1, int columns) const
{
    Envelope out;
    const StreamData *data = impl_->stream(stream);
    if (!data || columns <= 0 || std::isnan(t0) || std::isnan(t1) || t1 < t0) {
        return out;
    }
    const std::vector<double> &time = data->axis->time;
    const std::vector<float> &values = data->values;
    const std::size_t count = values.size();

    // Infinite bounds mean "the data's extent"; they would make every
    // column boundary NaN or infinite.
    if (std::isinf(t0)) {
        t0 = t0 < 0 ? time.front() : time.back();
    }
    if (std::isinf(t1)) {
        t1 = t1 < 0 ? time.front() : time.back();
    }
    if (t1 < t0) {
        return out;
    }

    // Column c holds [boundary(c), boundary(c + 1)); the last column is closed.
    // Each edge is searched for from a guess that the column holds as many
    // samples as the one before, which is exact for evenly sampled data and
    // keeps the search near memory already touched.
    const std::size_t column_count = static_cast<std::size_t>(columns);
    std::vector<std::size_t> edge(column_count + 1);
    const double span = t1 - t0;
    auto less = [](double sample, double bound) { return sample < bound; };
    auto less_equal = [](double sample, double bound) { return sample <= bound; };
    for (std::size_t c = 0; c <= column_count; c++) {
        if (c == 0) {
            edge[c] = firstNotBefore(time, 0, time.size() / 2, t0, less);
            continue;
        }
        const std::size_t from = edge[c - 1];
        const std::size_t step = c >= 2 ? edge[c - 1] - edge[c - 2] : 0;
        if (c == column_count) {
            edge[c] = firstNotBefore(time, from, from + step, t1, less_equal);
        } else {
            const double boundary =
                t0 + span * static_cast<double>(c) / static_cast<double>(columns);
            edge[c] = firstNotBefore(time, from, from + step, boundary, less);
        }
    }

    out.time.reserve(4 * column_count + 2);
    out.value.reserve(4 * column_count + 2);
    out.index.reserve(4 * column_count + 2);
    auto emit = [&](std::size_t index) {
        if (!out.index.empty() && out.index.back() >= index) {
            return;
        }
        out.index.push_back(index);
        out.time.push_back(time[index]);
        out.value.push_back(values[index]);
    };

    if (edge[0] > 0) {
        emit(edge[0] - 1);
    }
    for (std::size_t c = 0; c < column_count; c++) {
        const std::size_t begin = edge[c];
        const std::size_t end = edge[c + 1];
        if (begin >= end) {
            continue;
        }

        bool have = false;
        std::size_t min_index = begin;
        std::size_t max_index = begin;
        if (end - begin <= kDirectScanSamples) {
            for (std::size_t i = begin; i < end; i++) {
                const float v = values[i];
                if (std::isnan(v)) {
                    continue;
                }
                if (!have) {
                    have = true;
                    min_index = max_index = i;
                } else {
                    if (v < values[min_index]) {
                        min_index = i;
                    }
                    if (v > values[max_index]) {
                        max_index = i;
                    }
                }
            }
        } else {
            const MinMaxPyramid::Extremes extremes =
                data->pyramid.query(values.data(), begin, end, true);
            have = extremes.valid;
            min_index = extremes.min_index;
            max_index = extremes.max_index;
        }

        std::size_t points[4] = {begin, begin, begin, end - 1};
        if (have) {
            points[1] = std::min(min_index, max_index);
            points[2] = std::max(min_index, max_index);
        }
        for (std::size_t index : points) {
            emit(index);
        }
    }
    if (edge[column_count] < count) {
        emit(edge[column_count]);
    }
    return out;
}

std::optional<Sample> Log::valueAt(StreamId stream, double t, Pick pick) const
{
    const StreamData *data = impl_->stream(stream);
    if (!data || std::isnan(t)) {
        return std::nullopt;
    }
    const std::vector<double> &time = data->axis->time;
    const auto it = std::lower_bound(time.begin(), time.end(), t);
    std::size_t index = static_cast<std::size_t>(it - time.begin());
    if (index == time.size()) {
        index = time.size() - 1;
    } else if (pick == Pick::Nearest && index > 0) {
        const std::size_t before = index - 1;
        if (!(std::abs(time[index] - t) < std::abs(t - time[before]))) {
            index = before;
        }
    }

    Sample sample;
    sample.index = index;
    sample.time = time[index];
    sample.value = data->values[index];
    sample.exact = false;
    if (!data->info.derived && !data->info.value_column.empty()) {
        std::lock_guard<std::mutex> lock(impl_->db_mutex);
        Statement stmt(impl_->db, "SELECT " + quoted(data->info.value_column) + " FROM "
                                      + quoted(data->info.table) + " WHERE rowid = ?");
        if (stmt.valid()) {
            sqlite3_bind_int64(stmt.get(), 1, data->axis->rowid(index));
            if (stmt.next()) {
                sample.value = sqlite3_column_double(stmt.get(), 0);
                sample.exact = true;
            }
        }
    }
    return sample;
}

std::optional<std::pair<float, float>> Log::range(StreamId stream, double t0, double t1) const
{
    const StreamData *data = impl_->stream(stream);
    if (!data || std::isnan(t0) || std::isnan(t1) || t1 < t0) {
        return std::nullopt;
    }
    const auto indices = indexRange(data->axis->time, t0, t1);
    const MinMaxPyramid::Extremes extremes =
        data->pyramid.query(data->values.data(), indices.first, indices.second, false);
    if (!extremes.valid) {
        return std::nullopt;
    }
    return std::make_pair(extremes.min, extremes.max);
}

bool Log::forEachRow(const std::string &table, const std::vector<std::string> &columns,
                     const std::function<void(double time, const double *values)> &fn,
                     std::string &error) const
{
    std::shared_ptr<const Axis> axis;
    for (const std::unique_ptr<StreamData> &stream : impl_->streams) {
        if (stream && !stream->axis->table.empty() && stream->axis->table == table) {
            axis = stream->axis;
            break;
        }
    }
    if (!axis) {
        error = "No loaded stream uses table " + table;
        return false;
    }

    // A connection of its own, so a long pass neither blocks valueAt() nor
    // pays for SQLite's per-call locking.
    Connection connection;
    if (!openReadOnly(impl_->path, connection.db, error)) {
        return false;
    }
    std::string missing;
    if (!columnsExist(connection.db, axis->table, columns, missing)) {
        error = "Table " + table + " has no column " + missing;
        return false;
    }
    std::string sql = "SELECT rowid";
    for (const std::string &column : columns) {
        sql += ", " + quoted(column);
    }
    sql += " FROM " + quoted(axis->table) + " ORDER BY rowid";
    Statement stmt(connection.db, sql);
    if (!stmt.valid()) {
        error = "Failed to read " + table + ": " + stmt.error();
        return false;
    }

    const std::size_t count = axis->time.size();
    const std::size_t width = columns.size();
    const bool buffered = !axis->load_position.empty();
    std::vector<double> buffer(buffered ? count * width : 0);
    std::vector<sqlite3_int64> buffered_rowids(buffered ? count : 0);
    std::vector<double> row(width);
    sqlite3_stmt *raw = stmt.get();
    std::size_t position = 0;
    while (stmt.next()) {
        if (position >= count) {
            error = "Table " + table + " has more rows than when it was loaded";
            return false;
        }
        // Each row must be the one loaded at this position, or the file has
        // changed underneath us. Buffered rows are checked on delivery.
        const sqlite3_int64 rowid = sqlite3_column_int64(raw, 0);
        if (!buffered && rowid != axis->rowid(position)) {
            error = "Table " + table + " no longer matches the loaded rows";
            return false;
        }
        double *target = buffered ? &buffer[position * width] : row.data();
        for (std::size_t c = 0; c < width; c++) {
            target[c] = sqlite3_column_double(raw, static_cast<int>(c + 1));
        }
        if (buffered) {
            buffered_rowids[position] = rowid;
        } else {
            fn(axis->time[position], row.data());
        }
        position++;
    }
    if (stmt.failed()) {
        error = "Failed to read " + table + ": " + stmt.error();
        return false;
    }
    if (position != count) {
        error = "Table " + table + " has fewer rows than when it was loaded";
        return false;
    }
    if (buffered) {
        for (std::size_t i = 0; i < count; i++) {
            const std::size_t source = axis->load_position[i];
            if (buffered_rowids[source] != axis->rowid(i)) {
                error = "Table " + table + " no longer matches the loaded rows";
                return false;
            }
        }
        for (std::size_t i = 0; i < count; i++) {
            fn(axis->time[i], &buffer[axis->load_position[i] * width]);
        }
    }
    return true;
}

std::optional<StreamId> Log::addDerived(StreamInfo info, StreamId time_from,
                                        const std::vector<double> &values)
{
    return addDerived(std::move(info), time_from, values.data(), values.size());
}

std::optional<StreamId> Log::addDerived(StreamInfo info, StreamId time_from, const double *values,
                                        std::size_t count)
{
    const StreamData *source = impl_->stream(time_from);
    if (!source || !values || count != source->values.size()) {
        return std::nullopt;
    }
    auto stream = std::make_unique<StreamData>();
    stream->axis = source->axis;
    info.table = source->axis->table;
    info.time_column = source->axis->time_column;
    info.value_column.clear();
    info.derived = true;
    stream->info = std::move(info);
    stream->values.assign(values, values + count);
    finishStream(*stream);
    return impl_->add(std::move(stream));
}

std::optional<StreamId> Log::addComputed(StreamInfo info, const std::vector<StreamId> &inputs,
                                         const std::function<double(const float *values)> &fn)
{
    if (inputs.empty() || !fn) {
        return std::nullopt;
    }
    std::vector<const StreamData *> sources;
    for (StreamId id : inputs) {
        const StreamData *source = impl_->stream(id);
        if (!source || (!sources.empty() && source->axis != sources.front()->axis)) {
            return std::nullopt;
        }
        sources.push_back(source);
    }

    auto stream = std::make_unique<StreamData>();
    stream->axis = sources.front()->axis;
    info.table = stream->axis->table;
    info.time_column = stream->axis->time_column;
    info.value_column.clear();
    info.derived = true;
    stream->info = std::move(info);

    const std::size_t count = stream->axis->time.size();
    const std::size_t width = sources.size();
    stream->values.resize(count);
    std::vector<float> row(width);
    for (std::size_t i = 0; i < count; i++) {
        for (std::size_t j = 0; j < width; j++) {
            row[j] = sources[j]->values[i];
        }
        stream->values[i] = static_cast<float>(fn(row.data()));
    }
    finishStream(*stream);
    return impl_->add(std::move(stream));
}

std::optional<StreamId> Log::addDerived(StreamInfo info, std::vector<double> time,
                                        const std::vector<double> &values)
{
    if (time.empty() || time.size() != values.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < time.size(); i++) {
        if (std::isnan(time[i]) || (i > 0 && time[i] < time[i - 1])) {
            return std::nullopt;
        }
    }
    auto axis = std::make_shared<Axis>();
    axis->domain = info.domain;
    axis->time = std::move(time);
    auto stream = std::make_unique<StreamData>();
    stream->axis = std::move(axis);
    info.table.clear();
    info.time_column.clear();
    info.value_column.clear();
    info.derived = true;
    stream->info = std::move(info);
    stream->values.assign(values.begin(), values.end());
    finishStream(*stream);
    return impl_->add(std::move(stream));
}

bool Log::remove(StreamId stream)
{
    if (!impl_->stream(stream)) {
        return false;
    }
    impl_->streams[stream].reset();
    return true;
}

} // namespace sensordata
