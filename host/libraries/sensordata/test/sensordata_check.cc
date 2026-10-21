/**
 * @file    sensordata_check.cc
 * @brief   Offline assertion checks for the sensordata library.
 *
 * @details Builds small SQLite logs with the tagcore `streams` metadata
 *          layout, loads them with sensordata::Log, and compares every
 *          answer against an independent brute-force computation:
 *
 *          - view() against a linear-scan M4 on random and adversarial data
 *            (spikes, constant runs, signed zeros, infinities, NaN runs, a
 *            gap between segments), for windows inside, across and outside
 *            the data and column counts from 1 to more than the samples;
 *          - range() against a scan;
 *          - valueAt() returning exactly the stored `double`, for both picks,
 *            with contiguous and non-contiguous rowids;
 *          - the out-of-order fallback and forEachRow() delivery order;
 *          - NULL values, empty and single-row tables, metadata errors;
 *          - derived streams, remove() and find();
 *          - every committed fixture in host/docs/fixtures/sensorviz.
 *
 *          With `--log <path>` it instead loads that log, reports load time
 *          and peak memory, times M4 views of every stream over four window
 *          sizes, and checks each view against brute force.
 *
 *          Exits 0 when every check passes, 1 otherwise. Each failure is
 *          printed with its line.
 */
#include "sensordata.h"

#include <sqlite3.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#ifndef TAG_DESIGNS_SOURCE_DIR
/// Repository root, for the fixture paths; CMake passes the real one.
#define TAG_DESIGNS_SOURCE_DIR "."
#endif

namespace {

using sensordata::Envelope;
using sensordata::Log;
using sensordata::StreamId;

int g_failures = 0;
int g_checks = 0;

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                                   \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (!(condition)) {                                                                \
            g_failures++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
        }                                                                                  \
    } while (0)

constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Building test logs
// ---------------------------------------------------------------------------

/// Executes @p sql on @p db, aborting the program on error.
void exec(sqlite3 *db, const std::string &sql)
{
    char *message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        std::fprintf(stderr, "SQL failed: %s\n  %s\n", message ? message : "?", sql.c_str());
        std::exit(2);
    }
}

/// A fresh database at @p path with an empty `streams` table.
sqlite3 *createLog(const std::string &path)
{
    std::remove(path.c_str());
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::fprintf(stderr, "cannot create %s\n", path.c_str());
        std::exit(2);
    }
    exec(db, "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;");
    exec(db,
         "CREATE TABLE streams (stream_id TEXT, group_id TEXT, group_name TEXT, "
         "table_name TEXT, time_column TEXT, value_column TEXT, stream_kind TEXT, "
         "display_name TEXT, units TEXT)");
    return db;
}

/// Adds one `streams` row.
void addMetadata(sqlite3 *db, const std::string &id, const std::string &table,
                 const std::string &time_column, const std::string &value_column,
                 const std::string &kind = "scalar", const std::string &group = "")
{
    exec(db, "INSERT INTO streams VALUES ('" + id + "', '" + group + "', '" + group + "', '"
                 + table + "', '" + time_column + "', '" + value_column + "', '" + kind + "', '"
                 + id + " label', 'u')");
}

/// Inserts rows `(time, value...)` into @p table in the given order.
void insertRows(sqlite3 *db, const std::string &table, const std::vector<long long> &time,
                const std::vector<std::vector<double>> &columns)
{
    std::string sql = "INSERT INTO " + table + " VALUES (?";
    for (std::size_t c = 0; c < columns.size(); c++) {
        sql += ", ?";
    }
    sql += ")";
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    exec(db, "BEGIN");
    for (std::size_t i = 0; i < time.size(); i++) {
        sqlite3_reset(stmt);
        sqlite3_bind_int64(stmt, 1, time[i]);
        for (std::size_t c = 0; c < columns.size(); c++) {
            const double v = columns[c][i];
            if (std::isnan(v)) {
                sqlite3_bind_null(stmt, static_cast<int>(c + 2));
            } else {
                sqlite3_bind_double(stmt, static_cast<int>(c + 2), v);
            }
        }
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::fprintf(stderr, "insert failed: %s\n", sqlite3_errmsg(db));
            std::exit(2);
        }
    }
    exec(db, "COMMIT");
    sqlite3_finalize(stmt);
}

/// A scratch file path for check @p name.
std::string scratchPath(const std::string &name)
{
    const char *dir = std::getenv("TMPDIR");
    std::string base = dir && *dir ? dir : "/tmp";
    if (base.back() != '/') {
        base += '/';
    }
    return base + "sensordata_check_" + name + ".db3";
}

/// Opens @p path, failing the check (and returning null) on error.
std::unique_ptr<Log> openOrFail(const std::string &path)
{
    std::string error;
    std::unique_ptr<Log> log = Log::open(path, error);
    if (!log) {
        std::fprintf(stderr, "open %s failed: %s\n", path.c_str(), error.c_str());
    }
    CHECK(log != nullptr);
    return log;
}

/// Handle of stream @p id, failing the check when absent.
StreamId handle(const Log &log, const std::string &id)
{
    const auto found = log.find(id);
    CHECK(found.has_value());
    return found.value_or(sensordata::kNoStream);
}

// ---------------------------------------------------------------------------
// Brute-force reference
// ---------------------------------------------------------------------------

/// Indices a correct M4 view emits, computed with linear scans only.
std::vector<std::size_t> bruteM4(const std::vector<double> &time, const std::vector<float> &value,
                                 double t0, double t1, int columns)
{
    std::vector<std::size_t> out;
    const std::size_t n = time.size();
    if (n == 0 || columns <= 0 || std::isnan(t0) || std::isnan(t1) || t1 < t0) {
        return out;
    }
    if (std::isinf(t0)) {
        t0 = t0 < 0 ? time.front() : time.back();
    }
    if (std::isinf(t1)) {
        t1 = t1 < 0 ? time.front() : time.back();
    }
    if (t1 < t0) {
        return out;
    }
    // One forward sweep: boundaries never decrease, so neither do edges.
    std::vector<std::size_t> edge(static_cast<std::size_t>(columns) + 1, n);
    std::size_t i = 0;
    for (int c = 0; c < columns; c++) {
        const double boundary = t0 + (t1 - t0) * static_cast<double>(c) / static_cast<double>(columns);
        while (i < n && time[i] < boundary) {
            i++;
        }
        edge[static_cast<std::size_t>(c)] = i;
    }
    while (i < n && time[i] <= t1) {
        i++;
    }
    edge[static_cast<std::size_t>(columns)] = i;
    auto emit = [&](std::size_t index) {
        if (out.empty() || out.back() < index) {
            out.push_back(index);
        }
    };
    if (edge[0] > 0) {
        emit(edge[0] - 1);
    }
    for (int c = 0; c < columns; c++) {
        const std::size_t a = edge[static_cast<std::size_t>(c)];
        const std::size_t b = edge[static_cast<std::size_t>(c) + 1];
        if (a >= b) {
            continue;
        }
        bool have = false;
        std::size_t lo = a;
        std::size_t hi = a;
        for (std::size_t k = a; k < b; k++) {
            if (std::isnan(value[k])) {
                continue;
            }
            if (!have) {
                have = true;
                lo = hi = k;
            }
            if (value[k] < value[lo]) {
                lo = k;
            }
            if (value[k] > value[hi]) {
                hi = k;
            }
        }
        emit(a);
        if (have) {
            emit(std::min(lo, hi));
            emit(std::max(lo, hi));
        }
        emit(b - 1);
    }
    if (edge[static_cast<std::size_t>(columns)] < n) {
        emit(edge[static_cast<std::size_t>(columns)]);
    }
    return out;
}

/// True when @p a and @p b are the same float, treating NaN as equal to NaN and -0 as distinct from +0.
bool sameFloat(float a, float b)
{
    return std::memcmp(&a, &b, sizeof a) == 0 || (std::isnan(a) && std::isnan(b));
}

/// Checks one view of @p stream against brute force over @p time / @p value; returns true if exact.
bool viewMatches(const Log &log, StreamId stream, const std::vector<double> &time,
                 const std::vector<float> &value, double t0, double t1, int columns)
{
    const Envelope got = log.view(stream, t0, t1, columns);
    const std::vector<std::size_t> want = bruteM4(time, value, t0, t1, columns);
    bool ok = got.index == want && got.time.size() == want.size()
        && got.value.size() == want.size();
    for (std::size_t k = 0; ok && k < want.size(); k++) {
        ok = got.time[k] == time[want[k]] && sameFloat(got.value[k], value[want[k]]);
    }
    if (!ok) {
        std::fprintf(stderr, "  view mismatch: t0=%.17g t1=%.17g columns=%d (got %zu points, want %zu)\n",
                     t0, t1, columns, got.index.size(), want.size());
    }
    return ok;
}

/// Min/max of the non-NaN values with time in [t0, t1], by scan.
bool bruteRange(const std::vector<double> &time, const std::vector<float> &value, double t0,
                double t1, float &lo, float &hi)
{
    bool have = false;
    for (std::size_t i = 0; i < time.size(); i++) {
        if (time[i] < t0 || time[i] > t1 || std::isnan(value[i])) {
            continue;
        }
        if (!have) {
            lo = hi = value[i];
            have = true;
        }
        lo = std::min(lo, value[i]);
        hi = std::max(hi, value[i]);
    }
    return have;
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

/// view() and range() on random and adversarial data with a gap between segments.
void checkViews()
{
    const std::string path = scratchPath("views");
    sqlite3 *db = createLog(path);
    exec(db, "CREATE TABLE Data (ElapsedUs INTEGER, a REAL, b REAL)");
    addMetadata(db, "a", "Data", "ElapsedUs", "a");
    addMetadata(db, "b", "Data", "ElapsedUs", "b");

    // Two segments of 400 Hz data, 1000 s apart; N is not a multiple of 32.
    const std::size_t n = 200003;
    std::vector<long long> time_us(n);
    std::vector<double> a(n);
    std::vector<double> b(n);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> lsb(-2000, 2000);
    for (std::size_t i = 0; i < n; i++) {
        time_us[i] = static_cast<long long>(i) * 2500 + (i >= 100000 ? 1000000000LL : 0);
        a[i] = lsb(rng) * 0.061;
        b[i] = std::sin(static_cast<double>(i) / 5000.0) * 100.0;
    }
    // Spikes at bucket and power-of-two boundaries and their neighbours.
    for (std::size_t k = 31; k < n; k = k * 2 + 1) {
        a[k] = 1.0e6;
        if (k + 1 < n) {
            a[k + 1] = -1.0e6;
        }
    }
    // A constant run, with the extreme repeated so the first index matters.
    for (std::size_t i = 50000; i < 52000; i++) {
        a[i] = 7.0;
    }
    a[50500] = 5.0e6;
    a[51700] = 5.0e6;
    // Signed zeros, infinities, a NaN run longer than a pyramid bucket and
    // isolated NULLs (which read back as 0 through sqlite3_column_double).
    for (std::size_t i = 150000; i < 150064; i++) {
        b[i] = (i % 2) ? 0.0 : -0.0;
    }
    b[160000] = kInf;
    b[160001] = -kInf;
    a[170000] = std::nan("");
    insertRows(db, "Data", time_us, {a, b});
    sqlite3_close(db);

    std::unique_ptr<Log> log = openOrFail(path);
    if (!log) {
        return;
    }

    // The expected drawing values are the stored doubles narrowed to float;
    // NULL reads back as 0.
    // SQLite also stores an integral REAL such as -0.0 as the integer 0, so
    // the expected stored values are read back rather than assumed.
    std::vector<double> time(n);
    std::vector<float> fa(n);
    std::vector<float> fb(n);
    {
        sqlite3 *readback = nullptr;
        sqlite3_open_v2(path.c_str(), &readback, SQLITE_OPEN_READONLY, nullptr);
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(readback, "SELECT a, b FROM Data ORDER BY rowid", -1, &stmt, nullptr);
        for (std::size_t i = 0; i < n && sqlite3_step(stmt) == SQLITE_ROW; i++) {
            time[i] = static_cast<double>(time_us[i]) / 1000000.0;
            fa[i] = static_cast<float>(sqlite3_column_double(stmt, 0));
            fb[i] = static_cast<float>(sqlite3_column_double(stmt, 1));
        }
        sqlite3_finalize(stmt);
        sqlite3_close(readback);
    }
    CHECK(fa[170000] == 0.0f);

    const StreamId sa = handle(*log, "a");
    const StreamId sb = handle(*log, "b");
    const double first = time.front();
    const double last = time.back();
    const double gap_start = time[99999];
    const double gap_end = time[100000];
    struct Window {
        double t0;
        double t1;
    };
    const Window windows[] = {
        {-kInf, kInf},
        {first, last},
        {first - 100.0, last + 100.0},
        {gap_start - 3.0, gap_end + 3.0},
        {gap_start + 1.0, gap_end - 1.0},     // inside the gap
        {last + 1.0, last + 2.0},             // wholly after
        {first - 2.0, first - 1.0},           // wholly before
        {time[50000], time[52000]},
        {time[150010], time[150010]},         // zero width, on a sample
        {time[150010] + 1e-4, time[150011] - 1e-4}, // zero samples, between two
        {time[159990], time[160020]},
        {time[31], time[31 + 64]},
        {time[1000], time[1000] + 0.5},
    };
    const int column_counts[] = {1, 2, 7, 64, 640, 2000, 100000};
    bool all_ok = true;
    for (const Window &w : windows) {
        for (int columns : column_counts) {
            all_ok = viewMatches(*log, sa, time, fa, w.t0, w.t1, columns) && all_ok;
            all_ok = viewMatches(*log, sb, time, fb, w.t0, w.t1, columns) && all_ok;
        }
    }
    // Random windows and widths, including columns of exactly the
    // direct-scan threshold and many columns a few samples wide.
    std::uniform_real_distribution<double> where(first - 10.0, last + 10.0);
    std::uniform_int_distribution<int> width(1, 3000);
    for (int r = 0; r < 300; r++) {
        double t0 = where(rng);
        double t1 = where(rng);
        if (t1 < t0) {
            std::swap(t0, t1);
        }
        const int columns = width(rng);
        all_ok = viewMatches(*log, sa, time, fa, t0, t1, columns) && all_ok;
        all_ok = viewMatches(*log, sb, time, fb, t0, t1, columns) && all_ok;
    }
    CHECK(all_ok);

    // Degenerate requests.
    CHECK(log->view(sa, first, last, 0).index.empty());
    CHECK(log->view(sa, last, first, 10).index.empty());
    CHECK(log->view(sa, std::nan(""), last, 10).index.empty());
    CHECK(log->view(sensordata::kNoStream, first, last, 10).index.empty());
    CHECK(log->view(sa, first, last, 2000).index.size() <= 4 * 2000 + 2);

    // range() against a scan, and the whole-stream summary.
    bool ranges_ok = true;
    for (const Window &w : windows) {
        for (StreamId s : {sa, sb}) {
            const std::vector<float> &v = s == sa ? fa : fb;
            float lo = 0.0f;
            float hi = 0.0f;
            const bool have = bruteRange(time, v, w.t0, w.t1, lo, hi);
            const auto got = log->range(s, w.t0, w.t1);
            ranges_ok = ranges_ok && got.has_value() == have
                && (!have || (got->first == lo && got->second == hi));
        }
    }
    CHECK(ranges_ok);
    const sensordata::StreamInfo *info = log->info(sb);
    CHECK(info != nullptr && info->samples == n);
    CHECK(info != nullptr && info->min == -std::numeric_limits<float>::infinity());
    CHECK(info != nullptr && info->max == std::numeric_limits<float>::infinity());
    CHECK(info != nullptr && info->first_time == first && info->last_time == last);
    CHECK(info != nullptr && info->domain == sensordata::TimeDomain::ElapsedSeconds);

    // A NaN run that covers whole buckets, through a derived stream (SQLite
    // cannot store NaN).
    std::vector<double> with_nan(b);
    std::vector<float> fnan(n);
    for (std::size_t i = 0; i < n; i++) {
        fnan[i] = static_cast<float>(b[i]);
    }
    for (std::size_t i = 180000; i < 180300; i++) {
        with_nan[i] = std::nan("");
        fnan[i] = std::nanf("");
    }
    sensordata::StreamInfo derived_info;
    derived_info.id = "b_nan";
    const auto snan = log->addDerived(derived_info, sb, with_nan);
    CHECK(snan.has_value());
    if (snan) {
        bool nan_ok = true;
        for (int columns : {1, 3, 100, 5000}) {
            nan_ok = viewMatches(*log, *snan, time, fnan, time[179990], time[180310], columns)
                && nan_ok;
            nan_ok = viewMatches(*log, *snan, time, fnan, time[180001], time[180299], columns)
                && nan_ok;
            nan_ok = viewMatches(*log, *snan, time, fnan, -kInf, kInf, columns) && nan_ok;
            // Signed zeros survive in a derived stream; the first zero wins.
            nan_ok = viewMatches(*log, *snan, time, fnan, time[149990], time[150070], columns)
                && nan_ok;
        }
        CHECK(nan_ok);
        CHECK(!log->range(*snan, time[180000], time[180299]).has_value());
    }
}

/// valueAt() returns the stored double, with both picks, contiguous or not.
void checkValueAt()
{
    const std::string path = scratchPath("value_at");
    sqlite3 *db = createLog(path);
    exec(db, "CREATE TABLE Data (Epoch INTEGER, v REAL)");
    addMetadata(db, "v", "Data", "Epoch", "v");
    const std::size_t n = 1000;
    std::vector<long long> epoch(n);
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; i++) {
        epoch[i] = 1767225600LL + static_cast<long long>(i) * 10;
        v[i] = 0.1 * static_cast<double>(i) + 1e-12; // not representable as float
    }
    insertRows(db, "Data", epoch, {v});
    // Non-contiguous rowids: drop every third row.
    exec(db, "DELETE FROM Data WHERE rowid % 3 = 0");
    sqlite3_close(db);

    std::vector<double> time;
    std::vector<double> value;
    for (std::size_t i = 0; i < n; i++) {
        if ((i + 1) % 3 != 0) {
            time.push_back(static_cast<double>(epoch[i]));
            value.push_back(v[i]);
        }
    }

    std::unique_ptr<Log> log = openOrFail(path);
    if (!log) {
        return;
    }
    const StreamId s = handle(*log, "v");
    CHECK(log->info(s) && log->info(s)->domain == sensordata::TimeDomain::EpochSeconds);
    bool ok = true;
    for (std::size_t i = 0; i < time.size(); i++) {
        for (double offset : {-4.0, 0.0, 4.0, 5.0, 6.0}) {
            const double t = time[i] + offset;
            // Reference picks by linear scan.
            std::size_t at_or_after = time.size() - 1;
            for (std::size_t k = 0; k < time.size(); k++) {
                if (time[k] >= t) {
                    at_or_after = k;
                    break;
                }
            }
            std::size_t nearest = 0;
            for (std::size_t k = 1; k < time.size(); k++) {
                if (std::abs(time[k] - t) < std::abs(time[nearest] - t)) {
                    nearest = k;
                }
            }
            const auto a = log->valueAt(s, t, sensordata::Pick::AtOrAfter);
            const auto b = log->valueAt(s, t, sensordata::Pick::Nearest);
            ok = ok && a && b && a->exact && b->exact && a->index == at_or_after
                && a->value == value[at_or_after] && a->time == time[at_or_after]
                && b->index == nearest && b->value == value[nearest];
        }
    }
    CHECK(ok);
    CHECK(!log->valueAt(s, std::nan("")).has_value());
    CHECK(log->valueAt(s, -kInf)->index == 0);
    CHECK(log->valueAt(s, kInf)->index == time.size() - 1);
}

/// Out-of-order rows are sorted stably, and forEachRow() follows the sorted order.
void checkOutOfOrder()
{
    const std::string path = scratchPath("out_of_order");
    sqlite3 *db = createLog(path);
    exec(db, "CREATE TABLE Data (StartElapsedUs INTEGER, x REAL, y REAL)");
    addMetadata(db, "x", "Data", "StartElapsedUs", "x", "record_column", "xy");
    addMetadata(db, "y", "Data", "StartElapsedUs", "y", "record_column", "xy");
    const std::size_t n = 5000;
    std::vector<long long> time_us(n);
    std::vector<double> x(n);
    std::vector<double> y(n);
    std::mt19937 rng(7);
    for (std::size_t i = 0; i < n; i++) {
        time_us[i] = static_cast<long long>(rng() % 2000) * 1000; // many equal times
        x[i] = static_cast<double>(i) + 0.25;                     // rowid order marker
        y[i] = std::sqrt(static_cast<double>(i));
    }
    insertRows(db, "Data", time_us, {x, y});
    sqlite3_close(db);

    // Reference: stable sort of rowid order by time.
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; i++) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t p, std::size_t q) { return time_us[p] < time_us[q]; });
    std::vector<double> time(n);
    std::vector<float> fx(n);
    for (std::size_t i = 0; i < n; i++) {
        time[i] = static_cast<double>(time_us[order[i]]) / 1000000.0;
        fx[i] = static_cast<float>(x[order[i]]);
    }

    std::unique_ptr<Log> log = openOrFail(path);
    if (!log) {
        return;
    }
    const StreamId sx = handle(*log, "x");
    CHECK(log->info(sx) && log->info(sx)->kind == "record_column"
          && log->info(sx)->group_id == "xy");
    CHECK(viewMatches(*log, sx, time, fx, -kInf, kInf, 100000));
    CHECK(viewMatches(*log, sx, time, fx, -kInf, kInf, 37));

    bool exact = true;
    for (std::size_t i = 0; i < n; i += 13) {
        const auto sample = log->valueAt(sx, time[i], sensordata::Pick::AtOrAfter);
        // AtOrAfter selects the first of equal times, the lowest rowid.
        std::size_t first = i;
        while (first > 0 && time[first - 1] == time[i]) {
            first--;
        }
        exact = exact && sample && sample->exact && sample->index == first
            && sample->value == x[order[first]];
    }
    CHECK(exact);

    std::size_t row = 0;
    bool rows_ok = true;
    std::string error;
    const bool streamed = log->forEachRow(
        "Data", {"y", "x"},
        [&](double t, const double *values) {
            rows_ok = rows_ok && row < n && t == time[row] && values[0] == y[order[row]]
                && values[1] == x[order[row]];
            row++;
        },
        error);
    CHECK(streamed);
    CHECK(rows_ok && row == n);
}

/// Edge-case tables and metadata errors.
void checkEdgeCases()
{
    {
        const std::string path = scratchPath("edges");
        sqlite3 *db = createLog(path);
        exec(db, "CREATE TABLE One (Epoch INTEGER, v REAL)");
        exec(db, "CREATE TABLE Empty (Epoch INTEGER, v REAL)");
        exec(db, "INSERT INTO One VALUES (1767225600, 3.5)");
        addMetadata(db, "empty", "Empty", "Epoch", "v");
        addMetadata(db, "one", "One", "Epoch", "v");
        sqlite3_close(db);

        std::unique_ptr<Log> log = openOrFail(path);
        if (log) {
            const std::vector<sensordata::StreamInfo> streams = log->streams();
            CHECK(streams.size() == 1 && streams[0].id == "one");
            CHECK(!log->find("empty").has_value());
            const StreamId s = handle(*log, "one");
            const std::vector<double> time = {1767225600.0};
            const std::vector<float> value = {3.5f};
            for (int columns : {1, 5, 2000}) {
                CHECK(viewMatches(*log, s, time, value, -kInf, kInf, columns));
                CHECK(viewMatches(*log, s, time, value, time[0] - 10.0, time[0] + 10.0, columns));
                CHECK(viewMatches(*log, s, time, value, time[0] + 1.0, time[0] + 10.0, columns));
                CHECK(viewMatches(*log, s, time, value, time[0] - 10.0, time[0] - 1.0, columns));
            }
            const auto sample = log->valueAt(s, 0.0);
            CHECK(sample && sample->exact && sample->value == 3.5 && sample->index == 0);
        }
    }

    // Metadata that must be refused.
    struct Bad {
        const char *name;
        const char *sql;
    };
    const Bad bad[] = {
        {"no_streams_rows", "SELECT 1"},
        {"missing_table", "INSERT INTO streams VALUES ('a','','','Nope','Epoch','v','scalar','A','')"},
        {"bad_kind", "CREATE TABLE T (Epoch INTEGER, v REAL);"
                     "INSERT INTO streams VALUES ('a','','','T','Epoch','v','vector','A','')"},
        {"incomplete", "CREATE TABLE T (Epoch INTEGER, v REAL);"
                       "INSERT INTO streams VALUES ('a','','','T','Epoch','','scalar','A','')"},
        {"ungrouped_record", "CREATE TABLE T (Epoch INTEGER, v REAL);"
                             "INSERT INTO streams VALUES ('a','','','T','Epoch','v','record_column','A','')"},
        {"mixed_record", "CREATE TABLE T (Epoch INTEGER, v REAL); CREATE TABLE U (Epoch INTEGER, v REAL);"
                         "INSERT INTO streams VALUES ('a','g','g','T','Epoch','v','record_column','A','');"
                         "INSERT INTO streams VALUES ('b','g','g','U','Epoch','v','record_column','B','')"},
        {"missing_column", "CREATE TABLE T (Epoch INTEGER, v REAL);"
                           "INSERT INTO streams VALUES ('a','','','T','Epoch','w','scalar','A','')"},
    };
    for (const Bad &b : bad) {
        const std::string path = scratchPath(b.name);
        sqlite3 *db = createLog(path);
        exec(db, b.sql);
        sqlite3_close(db);
        std::string error;
        const std::unique_ptr<Log> log = Log::open(path, error);
        if (log) {
            std::fprintf(stderr, "  accepted bad metadata case %s\n", b.name);
        }
        CHECK(log == nullptr);
        CHECK(!error.empty());
    }
    {
        const std::string path = scratchPath("no_metadata");
        std::remove(path.c_str());
        sqlite3 *db = nullptr;
        sqlite3_open(path.c_str(), &db);
        exec(db, "CREATE TABLE T (Epoch INTEGER)");
        sqlite3_close(db);
        std::string error;
        CHECK(Log::open(path, error) == nullptr);
        CHECK(Log::open(scratchPath("does_not_exist_dir/x"), error) == nullptr);
    }
}

/// Derived streams, find(), remove() and forEachRow() after removal.
void checkDerived()
{
    const std::string path = scratchPath("derived");
    sqlite3 *db = createLog(path);
    exec(db, "CREATE TABLE Data (ElapsedUs INTEGER, ax REAL, ay REAL)");
    addMetadata(db, "ax", "Data", "ElapsedUs", "ax");
    addMetadata(db, "ay", "Data", "ElapsedUs", "ay");
    const std::size_t n = 3000;
    std::vector<long long> time_us(n);
    std::vector<double> ax(n);
    std::vector<double> ay(n);
    for (std::size_t i = 0; i < n; i++) {
        time_us[i] = static_cast<long long>(i) * 2500;
        ax[i] = std::cos(static_cast<double>(i) * 0.01) * 1000.0;
        ay[i] = std::sin(static_cast<double>(i) * 0.01) * 1000.0;
    }
    insertRows(db, "Data", time_us, {ax, ay});
    sqlite3_close(db);

    std::unique_ptr<Log> log = openOrFail(path);
    if (!log) {
        return;
    }
    const StreamId sax = handle(*log, "ax");
    const StreamId say = handle(*log, "ay");

    // A magnitude computed from forEachRow() doubles, as sensorviz does.
    std::vector<double> magnitude;
    std::string error;
    CHECK(log->forEachRow("Data", {"ax", "ay"},
                          [&](double, const double *v) {
                              magnitude.push_back(std::sqrt(v[0] * v[0] + v[1] * v[1]));
                          },
                          error));
    CHECK(magnitude.size() == n);
    CHECK(!log->forEachRow("Data", {"nope"}, [](double, const double *) {}, error));
    CHECK(!log->forEachRow("Nope", {"ax"}, [](double, const double *) {}, error));
    sensordata::StreamInfo info;
    info.id = "magnitude";
    info.label = "Magnitude";
    info.value_column = "should be cleared";
    const auto smag = log->addDerived(info, sax, magnitude);
    CHECK(smag.has_value());
    if (smag) {
        const sensordata::StreamInfo *got = log->info(*smag);
        CHECK(got && got->derived && got->value_column.empty() && got->table == "Data"
              && got->samples == n && got->domain == sensordata::TimeDomain::ElapsedSeconds);
        const auto sample = log->valueAt(*smag, 1.0);
        CHECK(sample && !sample->exact
              && sample->value == static_cast<double>(static_cast<float>(magnitude[sample->index])));
    }
    CHECK(!log->addDerived(info, sax, std::vector<double>(n - 1)).has_value());
    CHECK(!log->addDerived(info, sensordata::kNoStream, magnitude).has_value());
    CHECK(!log->addDerived(info, sax, nullptr, 0).has_value());
    const auto by_pointer = log->addDerived(info, sax, magnitude.data(), magnitude.size());
    CHECK(by_pointer.has_value() && log->remove(*by_pointer));

    // A replacement registered under a stored id is what find() returns.
    sensordata::StreamInfo corrected;
    corrected.id = "ax";
    std::vector<double> ax_corrected(ax);
    for (double &value : ax_corrected) {
        value -= 12.5;
    }
    const auto sfix = log->addDerived(corrected, sax, ax_corrected);
    CHECK(sfix.has_value() && log->find("ax") == sfix);
    CHECK(log->remove(sax));
    CHECK(!log->remove(sax));
    CHECK(log->info(sax) == nullptr);
    CHECK(log->find("ax") == sfix);
    CHECK(log->view(sax, -kInf, kInf, 10).index.empty());

    // The table stays readable while any stream uses its axis.
    CHECK(log->remove(say));
    std::size_t rows = 0;
    CHECK(log->forEachRow("Data", {"ax"}, [&](double, const double *) { rows++; }, error));
    CHECK(rows == n);
    CHECK(log->remove(*smag));
    CHECK(log->remove(*sfix));
    CHECK(!log->forEachRow("Data", {"ax"}, [](double, const double *) {}, error));

    // A derived stream with its own axis.
    sensordata::StreamInfo own;
    own.id = "own";
    own.domain = sensordata::TimeDomain::EpochSeconds;
    const auto sown = log->addDerived(own, {1.0, 2.0, 2.0, 3.0}, {4.0, -1.0, 9.0, 0.5});
    CHECK(sown.has_value());
    if (sown) {
        const std::vector<double> time = {1.0, 2.0, 2.0, 3.0};
        const std::vector<float> value = {4.0f, -1.0f, 9.0f, 0.5f};
        CHECK(viewMatches(*log, *sown, time, value, -kInf, kInf, 1));
        CHECK(viewMatches(*log, *sown, time, value, 1.5, 2.5, 3));
        CHECK(log->info(*sown)->table.empty());
        CHECK(log->info(*sown)->domain == sensordata::TimeDomain::EpochSeconds);
    }
    CHECK(!log->addDerived(own, {2.0, 1.0}, {0.0, 0.0}).has_value());
    CHECK(!log->addDerived(own, {1.0, std::nan("")}, {0.0, 0.0}).has_value());
    CHECK(!log->addDerived(own, std::vector<double>{}, {}).has_value());
    CHECK(log->streams().size() == 1);
}

/// Reads one column of @p table ordered by @p order as doubles.
std::vector<double> sqlColumn(sqlite3 *db, const std::string &table, const std::string &column,
                              const std::string &order, bool as_int64)
{
    std::vector<double> out;
    sqlite3_stmt *stmt = nullptr;
    const std::string sql = "SELECT \"" + column + "\" FROM \"" + table + "\" ORDER BY " + order;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(as_int64 ? static_cast<double>(sqlite3_column_int64(stmt, 0))
                               : sqlite3_column_double(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return out;
}

/// Every committed sensorviz fixture loads with the streams its metadata names.
void checkFixtures()
{
    const char *fixtures[] = {"imutag.db3", "compasstag.db3"};
    for (const char *name : fixtures) {
        const std::string path =
            std::string(TAG_DESIGNS_SOURCE_DIR) + "/host/docs/fixtures/sensorviz/" + name;
        std::unique_ptr<Log> log = openOrFail(path);
        if (!log) {
            continue;
        }
        sqlite3 *db = nullptr;
        sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);

        // Expected streams: every metadata row whose table has rows, in order.
        std::vector<std::string> expected;
        struct Row {
            std::string id, table, time_column, value_column;
        };
        std::vector<Row> rows;
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db,
                           "SELECT stream_id, table_name, time_column, value_column FROM streams "
                           "ORDER BY rowid",
                           -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Row row;
            row.id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            row.table = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            row.time_column = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            row.value_column = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            rows.push_back(row);
        }
        sqlite3_finalize(stmt);

        std::vector<std::string> got;
        for (const sensordata::StreamInfo &info : log->streams()) {
            got.push_back(info.id);
        }
        bool fixture_ok = true;
        for (const Row &row : rows) {
            // The time axis as sensorviz has always read it: ORDER BY time.
            const bool elapsed = row.time_column.size() >= 9
                && row.time_column.compare(row.time_column.size() - 9, 9, "ElapsedUs") == 0;
            std::vector<double> time =
                sqlColumn(db, row.table, row.time_column, "\"" + row.time_column + "\"", true);
            if (time.empty()) {
                continue;
            }
            expected.push_back(row.id);
            for (double &t : time) {
                t = elapsed ? t / 1000000.0 : t;
            }
            const std::vector<double> value =
                sqlColumn(db, row.table, row.value_column, "\"" + row.time_column + "\", rowid", false);
            std::vector<float> fvalue(value.begin(), value.end());
            const auto s = log->find(row.id);
            if (!s) {
                fixture_ok = false;
                continue;
            }
            const sensordata::StreamInfo *info = log->info(*s);
            fixture_ok = fixture_ok && info->samples == time.size()
                && info->domain
                    == (elapsed ? sensordata::TimeDomain::ElapsedSeconds
                                : sensordata::TimeDomain::EpochSeconds);
            fixture_ok = viewMatches(*log, *s, time, fvalue, -kInf, kInf, 1000) && fixture_ok;
            fixture_ok = viewMatches(*log, *s, time, fvalue, time.front(),
                                     time.front() + (time.back() - time.front()) / 7.0, 333)
                && fixture_ok;
            // forEachRow() hands back exactly the SQLite doubles, in time order.
            std::size_t i = 0;
            std::string error;
            fixture_ok = log->forEachRow(row.table, {row.value_column},
                                         [&](double t, const double *v) {
                                             fixture_ok = fixture_ok && i < time.size()
                                                 && t == time[i] && v[0] == value[i];
                                             i++;
                                         },
                                         error)
                && fixture_ok && i == time.size();
            const auto sample = log->valueAt(*s, time[time.size() / 2], sensordata::Pick::AtOrAfter);
            fixture_ok = fixture_ok && sample && sample->exact;
        }
        sqlite3_close(db);
        CHECK(got == expected);
        CHECK(fixture_ok);
        std::printf("  fixture %s: %zu streams\n", name, got.size());
    }
}

// ---------------------------------------------------------------------------
// --log mode
// ---------------------------------------------------------------------------

/// Peak physical footprint in bytes where the platform reports it, else peak RSS.
double peakMemoryBytes()
{
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count)
        == KERN_SUCCESS) {
        return static_cast<double>(info.ledger_phys_footprint_peak);
    }
#endif
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss);
#else
    return static_cast<double>(usage.ru_maxrss) * 1024.0;
#endif
}

using Clock = std::chrono::steady_clock;

/// Seconds since @p start.
double since(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/// Loads @p path, reports cost, and checks timed views of every stream against brute force.
int benchLog(const std::string &path)
{
    const auto start = Clock::now();
    std::string error;
    std::unique_ptr<Log> log = Log::open(path, error);
    if (!log) {
        std::fprintf(stderr, "open failed: %s\n", error.c_str());
        return 1;
    }
    const double load_s = since(start);
    const std::vector<sensordata::StreamInfo> streams = log->streams();
    std::size_t samples = 0;
    for (const sensordata::StreamInfo &info : streams) {
        samples += info.samples;
    }
    std::printf("load %.2f s, %zu streams, %zu samples, peak footprint %.0f MB\n", load_s,
                streams.size(), samples, peakMemoryBytes() / 1e6);

    // Views of the longest table's streams, centred, 2000 columns.
    std::size_t longest = 0;
    for (const sensordata::StreamInfo &info : streams) {
        longest = std::max(longest, info.samples);
    }
    std::vector<StreamId> big;
    for (const sensordata::StreamInfo &info : streams) {
        if (info.samples == longest) {
            big.push_back(info.handle);
        }
    }
    const sensordata::StreamInfo &first = *log->info(big.front());
    const double mid = (first.first_time + first.last_time) / 2.0;
    struct Window {
        const char *name;
        double seconds;
    };
    const Window windows[] = {{"full log", first.last_time - first.first_time},
                              {"1 hour", 3600.0},
                              {"1 minute", 60.0},
                              {"10 seconds", 10.0}};
    const int columns = 2000;
    const int reps = 20;
    for (const Window &w : windows) {
        const double t0 = mid - w.seconds / 2.0;
        const double t1 = mid + w.seconds / 2.0;
        std::size_t points = 0;
        const auto view_start = Clock::now();
        for (int r = 0; r < reps; r++) {
            points = 0;
            for (StreamId s : big) {
                points += log->view(s, t0, t1, columns).index.size();
            }
        }
        const double ms = since(view_start) * 1e3 / reps;
        std::printf("  view %-10s: %zu streams, %.2f ms, %zu points out\n", w.name, big.size(), ms,
                    points);
    }

    // Exactness: reconstruct each big stream's drawing values from
    // forEachRow() doubles, then compare every window by brute force.
    bool exact = true;
    for (StreamId s : big) {
        const sensordata::StreamInfo &info = *log->info(s);
        if (info.derived || info.table.empty()) {
            continue;
        }
        std::vector<double> time;
        std::vector<float> value;
        time.reserve(info.samples);
        value.reserve(info.samples);
        if (!log->forEachRow(info.table, {info.value_column},
                             [&](double t, const double *v) {
                                 time.push_back(t);
                                 value.push_back(static_cast<float>(v[0]));
                             },
                             error)) {
            std::fprintf(stderr, "forEachRow failed: %s\n", error.c_str());
            return 1;
        }
        for (const Window &w : windows) {
            exact = viewMatches(*log, s, time, value, mid - w.seconds / 2.0, mid + w.seconds / 2.0,
                                columns)
                && exact;
        }
        std::printf("  %s: %s\n", info.id.c_str(), exact ? "exact" : "MISMATCH");
    }
    return exact ? 0 : 1;
}

} // namespace

/**
 * @brief   Runs every check, or with `--log <path>` benchmarks that log.
 * @param[in] argc  Argument count: 1, or 3 for `--log <path>`.
 * @param[in] argv  Arguments.
 * @return  0 when every check passes, 1 on a failed check, 2 on bad usage.
 */
int main(int argc, char **argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--log") == 0) {
        return benchLog(argv[2]);
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: sensordata_check [--log <path>]\n");
        return 2;
    }

    checkViews();
    checkValueAt();
    checkOutOfOrder();
    checkEdgeCases();
    checkDerived();
    checkFixtures();

    std::printf("sensordata_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
