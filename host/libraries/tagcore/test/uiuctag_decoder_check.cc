/**
 * @file    uiuctag_decoder_check.cc
 * @brief   Drives the UIUCTag SQLite decoder with synthetic download blocks.
 *
 * @details Builds UIUCTagLog messages by hand and runs them through
 *          SqliteTagLogWriter, so the decoder can be developed and regression
 *          checked with no tag attached. The synthetic blocks cover what real
 *          captures produce only rarely or late: a full block, a partial block,
 *          an entirely erased block, an interior failed reading, an interior
 *          erased record, a minute not observed, activity at both edges, a
 *          malformed payload length, and an ACK carrying no log payload at all,
 *          which is how the tag signals end of log. It then checks the rows
 *          written by querying the database.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled.
 *
 * @see     embedded/tags/UIUCTag/design/test-strategy.md
 */

// These are assertion programs: keep assert() live in Release builds too,
// where CMake defines NDEBUG and would otherwise compile every check out.
#undef NDEBUG

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <tag.pb.h>
#include "sqlitelog.h"
#include "uiuctag_log_format.h"

/**
 * @brief Build a written record.
 *
 * @param[in] hpa Pressure in hPa.
 * @param[in] degc Temperature in degrees C.
 * @param[in] b0 Active seconds in bucket 0.
 * @param[in] b1 Active seconds in bucket 1.
 * @param[in] b2 Active seconds in bucket 2.
 * @return The record as the firmware would program it.
 */
static t_UIUCTagRecord record(double hpa, double degc, unsigned b0, unsigned b1,
                              unsigned b2)
{
    t_UIUCTagRecord r;
    uiuctagPut24(r.pressure, (uint32_t)std::lround(hpa * 6400.0));
    uiuctagPut24(r.temperature,
                 (uint32_t)std::lround(degc * 65536.0) & 0xFFFFFFu);
    r.activity = (uint16_t)(b0 | (b1 << 5) | (b2 << 10));
    return r;
}

/**
 * @brief Build a record in the erased state.
 *
 * @return Record with every byte 0xFF, as unwritten NOR flash reads.
 */
static t_UIUCTagRecord erasedRecord()
{
    t_UIUCTagRecord r;
    std::memset(&r, 0xFF, sizeof(r));
    return r;
}

/**
 * @brief Serialize records into a download payload.
 *
 * @param[in] records Records in block order, starting at record 0.
 * @return Byte image suitable for UIUCTagLog::set_samples().
 */
static std::string pack(const std::vector<t_UIUCTagRecord> &records)
{
    std::string out(records.size() * sizeof(t_UIUCTagRecord), '\0');
    if (!records.empty())
        std::memcpy(out.data(), records.data(), out.size());
    return out;
}

/**
 * @brief Write one block through the writer.
 *
 * @param[in,out] writer Open log writer.
 * @param[in] epoch Checkpoint epoch.
 * @param[in] payload Record bytes.
 * @return The writer's result.
 */
static int writeBlock(SqliteTagLogWriter &writer, int32_t epoch,
                      const std::string &payload)
{
    Ack ack;
    auto *log = ack.mutable_uiuctag_data_log();
    log->set_epoch(epoch);
    log->set_voltage(3.0f);
    log->set_samples(payload);
    return writer.writeLog(ack);
}

/**
 * @brief Run a single-value query.
 *
 * @param[in] db Open database.
 * @param[in] sql Query returning one numeric value.
 * @return The value, or NaN when the query returns no row.
 */
static double scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st = nullptr;
    double value = NAN;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
        value = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    printf("  %-70s -> %g\n", sql, value);
    return value;
}

/**
 * @brief Run the synthetic-block decoder checks.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional argv[1]: output database path.
 * @return 0 when every block decoded as expected; asserts on any mismatch and
 *         returns 1 when the database cannot be opened.
 */
int main(int argc, char **argv)
{
    const char *db_path = (argc > 1) ? argv[1] : "uiuctag_decoder_check.db3";
    const int32_t block0 = 1767225780;          // a minute boundary
    std::remove(db_path);

    Config config;
    config.set_tag_type(UIUCTAG);
    {
        SqliteTagLogWriter writer(db_path, config, true);
        if (!writer.isOpen() || !writer.beginLog()) {
            printf("FAIL: writer: %s\n", writer.lastError().c_str());
            return 1;
        }

        // Block A: 32 records. Record 0 is the first of a run (minute not
        // observed); record 7 a failed reading; record 11 never written;
        // the rest activity 0, 20 and 10 seconds.
        std::vector<t_UIUCTagRecord> a;
        for (unsigned i = 0; i < UIUCTAG_LOG_RECORDS; i++) {
            t_UIUCTagRecord r = record(1000.0 + i, 20.0 - i, 0, 20, 10);
            if (i == 0)
                r.activity = UIUCTAG_ACTIVITY_UNOBSERVED_WORD;
            if (i == 7) {
                uiuctagPut24(r.pressure, UIUCTAG_RAW_FAILED);
                uiuctagPut24(r.temperature, UIUCTAG_RAW_FAILED);
            }
            if (i == 11)
                r = erasedRecord();
            a.push_back(r);
        }
        assert(writeBlock(writer, block0, pack(a)) == 1);

        // Block B: partial, 5 records, opened after a gap.
        std::vector<t_UIUCTagRecord> b;
        for (unsigned i = 0; i < 5; i++)
            b.push_back(record(900.0 + i, -5.0, 20, 20, 20));
        assert(writeBlock(writer, block0 + 7200, pack(b)) == 1);

        // Block C: entirely erased.
        assert(writeBlock(writer, block0 + 14400,
                          pack(std::vector<t_UIUCTagRecord>(UIUCTAG_LOG_RECORDS,
                                                            erasedRecord()))) == 1);

        // Block D: malformed (not a whole number of records).
        assert(writeBlock(writer, block0 + 21600, std::string(13, '\0')) == -2);
        printf("block D rejected: %s\n", writer.lastError().c_str());

        // Block E: longer than one block.
        assert(writeBlock(writer, block0 + 28800, std::string(264, '\0')) == -2);

        // An ACK with no UIUCTag payload is end of log, not an error.
        Ack empty;
        assert(writer.writeLog(empty) == 0);

        if (!writer.endLog()) {
            printf("FAIL endLog: %s\n", writer.lastError().c_str());
            return 1;
        }
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        printf("FAIL: cannot reopen %s\n", db_path);
        return 1;
    }
    // A: 32 - failed - erased = 30 readings; B: 5.
    assert(scalar(db, "SELECT count(*) FROM Pressure") == 35);
    assert(scalar(db, "SELECT count(*) FROM Temperature") == 35);
    // A: 32 - unobserved - erased = 30 records x 3; B: 5 x 3.
    assert(scalar(db, "SELECT count(*) FROM Activity") == 105);
    assert(scalar(db, "SELECT count(*) FROM Voltage") == 3);
    // Failed reading at record 7 leaves no pressure row there...
    assert(scalar(db, "SELECT count(*) FROM Pressure WHERE Epoch = 1767226200") == 0);
    // ...but its activity minute is still recorded.
    assert(scalar(db, "SELECT count(*) FROM Activity WHERE Epoch >= 1767226140 "
                      "AND Epoch < 1767226200") == 3);
    // Record 1's buckets: 0, 100 and 50 percent, in the minute before it.
    assert(scalar(db, "SELECT Activity FROM Activity WHERE Epoch = 1767225780") == 0);
    assert(scalar(db, "SELECT Activity FROM Activity WHERE Epoch = 1767225800") == 100);
    assert(scalar(db, "SELECT Activity FROM Activity WHERE Epoch = 1767225820") == 50);
    // Record 0's minute was not observed.
    assert(std::isnan(scalar(db, "SELECT Activity FROM Activity WHERE Epoch = 1767225720")));
    // Conversions.
    assert(std::fabs(scalar(db, "SELECT Pressure FROM Pressure WHERE Epoch = 1767225840")
                     - 1001.0) < 1e-6);
    assert(std::fabs(scalar(db, "SELECT Temperature FROM Temperature WHERE Epoch = 1767225840")
                     - 19.0) < 1e-6);
    assert(std::fabs(scalar(db, "SELECT Temperature FROM Temperature WHERE Epoch = 1767232980")
                     + 5.0) < 1e-6);
    sqlite3_close(db);

    printf("uiuctag_decoder_check: all assertions passed\n");
    return 0;
}
