// Prototype benchmark for the sensorviz decimating backend proposal.
//
//   bench current <db>   load the six IMU streams the way sensorviz does today
//                        (one ORDER BY query per stream, double time+value each)
//   bench proposed <db>  load each table once in rowid order, shared int64 time
//                        axis, float32 values, min/max pyramid, then time M4
//                        view queries and check them against brute force.
#include <sqlite3.h>
#include <sys/resource.h>
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double secondsSince(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}
static double peakRssMB()
{
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss / 1e6; // macOS reports bytes
}

// Physical footprint (what Activity Monitor shows as Memory), including pages
// macOS has compressed; ru_maxrss excludes compressed pages.
static double footprintMB()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    return info.phys_footprint / 1e6;
}
static double peakFootprintMB()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    return info.ledger_phys_footprint_peak / 1e6;
}
static sqlite3 *openDb(const char *path)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "open failed\n");
        std::exit(1);
    }
    return db;
}

// ---------------------------------------------------------------- current
static int runCurrent(const char *path)
{
    sqlite3 *db = openDb(path);
    const char *streams[][2] = {{"ImuAccel", "ax"}, {"ImuAccel", "ay"}, {"ImuAccel", "az"},
                                {"ImuGyro", "gx"},  {"ImuGyro", "gy"},  {"ImuGyro", "gz"}};
    struct Stream { std::vector<double> time, value; };
    std::vector<Stream> loaded;
    const auto t0 = Clock::now();
    for (auto &s : streams) {
        const std::string sql = std::string("SELECT ElapsedUs, ") + s[1] + " FROM " + s[0]
                                + " ORDER BY ElapsedUs";
        sqlite3_stmt *st = nullptr;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr);
        Stream out;
        const auto ts = Clock::now();
        while (sqlite3_step(st) == SQLITE_ROW) {
            out.time.push_back(sqlite3_column_int64(st, 0) * 1e-6);
            out.value.push_back(sqlite3_column_double(st, 1));
        }
        sqlite3_finalize(st);
        std::printf("  %s.%s: %zu rows in %.1f s\n", s[0], s[1], out.time.size(), secondsSince(ts));
        loaded.push_back(std::move(out));
    }
    std::printf("current: load %.1f s, peak RSS %.0f MB, footprint %.0f MB, peak footprint %.0f MB (sensorviz additionally copies each "
                "visible graph into QCustomPlot)\n",
                secondsSince(t0), peakRssMB(), footprintMB(), peakFootprintMB());
    sqlite3_close(db);
    return 0;
}

// ---------------------------------------------------------------- proposed
constexpr size_t kBase = 32; // samples per level-0 bucket

struct Pyramid {
    // levels[k] holds min/max pairs for buckets of kBase << k samples.
    std::vector<std::vector<float>> mins, maxs;
};

struct Table {
    std::vector<int64_t> time_us;            // shared by every column of the table
    std::vector<std::vector<float>> values;  // one per column
    std::vector<Pyramid> pyramids;
};

static Table loadTable(sqlite3 *db, const char *table, const char *cols[3])
{
    Table t;
    t.values.resize(3);
    int64_t rows = 0;
    {
        sqlite3_stmt *st = nullptr;
        sqlite3_prepare_v2(db, (std::string("SELECT count(*) FROM ") + table).c_str(), -1, &st,
                           nullptr);
        sqlite3_step(st);
        rows = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    t.time_us.reserve(rows);
    for (auto &v : t.values) v.reserve(rows);
    const std::string sql = std::string("SELECT ElapsedUs, ") + cols[0] + ", " + cols[1] + ", "
                            + cols[2] + " FROM " + table + " ORDER BY rowid";
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr);
    bool monotonic = true;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const int64_t us = sqlite3_column_int64(st, 0);
        if (!t.time_us.empty() && us < t.time_us.back()) monotonic = false;
        t.time_us.push_back(us);
        for (int c = 0; c < 3; c++) t.values[c].push_back(float(sqlite3_column_double(st, c + 1)));
    }
    sqlite3_finalize(st);
    if (!monotonic) std::printf("  WARNING: %s time not monotonic in rowid order\n", table);
    return t;
}

static Pyramid buildPyramid(const std::vector<float> &v)
{
    Pyramid p;
    const size_t n0 = v.size() / kBase; // trailing partial bucket handled by raw scan
    p.mins.emplace_back(n0);
    p.maxs.emplace_back(n0);
    for (size_t b = 0; b < n0; b++) {
        const float *s = v.data() + b * kBase;
        float lo = s[0], hi = s[0];
        for (size_t i = 1; i < kBase; i++) { lo = std::min(lo, s[i]); hi = std::max(hi, s[i]); }
        p.mins[0][b] = lo;
        p.maxs[0][b] = hi;
    }
    while (p.mins.back().size() >= 2) {
        const auto &pm = p.mins.back(), &pM = p.maxs.back();
        const size_t n = pm.size() / 2;
        std::vector<float> m(n), M(n);
        for (size_t b = 0; b < n; b++) {
            m[b] = std::min(pm[2 * b], pm[2 * b + 1]);
            M[b] = std::max(pM[2 * b], pM[2 * b + 1]);
        }
        p.mins.push_back(std::move(m));
        p.maxs.push_back(std::move(M));
    }
    return p;
}

static size_t pyramidBytes(const Pyramid &p)
{
    size_t n = 0;
    for (auto &l : p.mins) n += l.size() * sizeof(float) * 2;
    return n;
}

// Min and max over samples [i0, i1): raw scan of the unaligned edges, pyramid
// buckets of the largest level that fits for the interior.
static void rangeMinMax(const std::vector<float> &v, const Pyramid &p, size_t i0, size_t i1,
                        float &lo, float &hi, size_t &touched)
{
    lo = INFINITY;
    hi = -INFINITY;
    size_t i = i0;
    while (i < i1) {
        // Largest bucket that starts at i and ends within [i, i1).
        int level = -1;
        for (int k = int(p.mins.size()) - 1; k >= 0; k--) {
            const size_t b = kBase << k;
            if (i % b == 0 && i + b <= i1 && i / b < p.mins[k].size()) { level = k; break; }
        }
        if (level < 0) {
            lo = std::min(lo, v[i]);
            hi = std::max(hi, v[i]);
            touched++;
            i++;
        } else {
            const size_t b = kBase << level;
            lo = std::min(lo, p.mins[level][i / b]);
            hi = std::max(hi, p.maxs[level][i / b]);
            touched++;
            i += b;
        }
    }
}

struct Column { size_t first, last; float lo, hi; };

// M4 envelope of samples [i0, i1) split into `pixels` columns.
static std::vector<Column> viewM4(const std::vector<float> &v, const Pyramid &p, size_t i0,
                                  size_t i1, int pixels, size_t &touched)
{
    std::vector<Column> out;
    out.reserve(pixels);
    for (int c = 0; c < pixels; c++) {
        const size_t a = i0 + (i1 - i0) * size_t(c) / pixels;
        const size_t b = i0 + (i1 - i0) * size_t(c + 1) / pixels;
        if (a >= b) continue;
        Column col{a, b - 1, 0, 0};
        rangeMinMax(v, p, a, b, col.lo, col.hi, touched);
        out.push_back(col);
    }
    return out;
}

static std::vector<Column> bruteM4(const std::vector<float> &v, size_t i0, size_t i1, int pixels)
{
    std::vector<Column> out;
    for (int c = 0; c < pixels; c++) {
        const size_t a = i0 + (i1 - i0) * size_t(c) / pixels;
        const size_t b = i0 + (i1 - i0) * size_t(c + 1) / pixels;
        if (a >= b) continue;
        Column col{a, b - 1, v[a], v[a]};
        for (size_t i = a; i < b; i++) { col.lo = std::min(col.lo, v[i]); col.hi = std::max(col.hi, v[i]); }
        out.push_back(col);
    }
    return out;
}

static int runProposed(const char *path)
{
    sqlite3 *db = openDb(path);
    const char *accel[3] = {"ax", "ay", "az"}, *gyro[3] = {"gx", "gy", "gz"};
    auto t0 = Clock::now();
    std::vector<Table> tables;
    tables.push_back(loadTable(db, "ImuAccel", accel));
    std::printf("  ImuAccel: %zu rows in %.1f s\n", tables[0].time_us.size(), secondsSince(t0));
    auto t1 = Clock::now();
    tables.push_back(loadTable(db, "ImuGyro", gyro));
    std::printf("  ImuGyro:  %zu rows in %.1f s\n", tables[1].time_us.size(), secondsSince(t1));
    const double load_s = secondsSince(t0);
    sqlite3_close(db);

    auto tp = Clock::now();
    size_t pyr_bytes = 0;
    for (auto &t : tables)
        for (auto &v : t.values) { t.pyramids.push_back(buildPyramid(v)); pyr_bytes += pyramidBytes(t.pyramids.back()); }
    const double pyr_s = secondsSince(tp);

    const size_t n = tables[0].time_us.size();
    std::printf("proposed: load %.1f s, pyramid build %.2f s, pyramids %.0f MB, data %.0f MB, "
                "peak RSS %.0f MB, footprint %.0f MB, peak footprint %.0f MB\n",
                load_s, pyr_s, pyr_bytes / 1e6,
                (2 * n * sizeof(int64_t) + 6 * n * sizeof(float)) / 1e6, peakRssMB(), footprintMB(),
                peakFootprintMB());

    // View queries: time window -> sample range by binary search on the shared
    // time axis, then M4 per stream, 2000 px wide, all six streams.
    const int px = 2000;
    const auto &time = tables[0].time_us;
    const double span_s = (time.back() - time.front()) * 1e-6;
    struct Win { const char *name; double seconds; } wins[] = {
        {"full log", span_s}, {"1 hour", 3600}, {"1 minute", 60}, {"10 seconds", 10}};
    bool all_ok = true;
    for (auto &w : wins) {
        const int64_t mid = (time.front() + time.back()) / 2;
        const int64_t half = int64_t(w.seconds * 0.5e6);
        const int reps = 20;
        size_t touched = 0, points = 0;
        auto tq = Clock::now();
        for (int r = 0; r < reps; r++) {
            const size_t i0 = std::lower_bound(time.begin(), time.end(), mid - half) - time.begin();
            const size_t i1 = std::upper_bound(time.begin(), time.end(), mid + half) - time.begin();
            touched = 0;
            points = 0;
            for (auto &t : tables)
                for (int c = 0; c < 3; c++) {
                    auto cols = viewM4(t.values[c], t.pyramids[c], i0, i1, px, touched);
                    points += std::min(cols.size() * 4, i1 - i0);
                }
        }
        const double ms = secondsSince(tq) * 1e3 / reps;
        // Exactness check against brute force on one stream per table.
        const size_t i0 = std::lower_bound(time.begin(), time.end(), mid - half) - time.begin();
        const size_t i1 = std::upper_bound(time.begin(), time.end(), mid + half) - time.begin();
        bool ok = true;
        double brute_ms = 0;
        for (auto &t : tables) {
            size_t dummy = 0;
            auto fast = viewM4(t.values[0], t.pyramids[0], i0, i1, px, dummy);
            auto tb = Clock::now();
            auto slow = bruteM4(t.values[0], i0, i1, px);
            brute_ms += secondsSince(tb) * 1e3;
            ok = ok && fast.size() == slow.size();
            for (size_t k = 0; ok && k < fast.size(); k++)
                ok = fast[k].lo == slow[k].lo && fast[k].hi == slow[k].hi
                     && fast[k].first == slow[k].first && fast[k].last == slow[k].last;
        }
        all_ok = all_ok && ok;
        std::printf("  view %-10s: %9zu samples/stream, %.2f ms for 6 streams, %zu entries touched, "
                    "%zu points out (%.0f KB as float32 x+y); brute force %.0f ms/stream; %s\n",
                    w.name, i1 - i0, ms, touched, points, points * 8 / 1e3, brute_ms / 2,
                    ok ? "exact" : "MISMATCH");
    }
    return all_ok ? 0 : 2;
}

int main(int argc, char **argv)
{
    if (argc != 3) { std::fprintf(stderr, "usage: bench current|proposed <db>\n"); return 1; }
    if (!std::strcmp(argv[1], "current")) return runCurrent(argv[2]);
    if (!std::strcmp(argv[1], "proposed")) return runProposed(argv[2]);
    return 1;
}
