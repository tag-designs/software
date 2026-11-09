/**
 * @file    prestaghr_decoder_check.cc
 * @brief   Drives the PresTagHR SQLite decoder with synthetic download blocks.
 *
 * @details Builds PresTagHRLog messages by hand, as the firmware's
 *          data_logAck() packs them, and runs them through SqliteTagLogWriter.
 *          Covers what a short bench run rarely shows: positive and negative
 *          24-bit pressures (sign extension), the low byte that PresTag drops,
 *          a negative temperature, an interior failed read that must keep its
 *          slot, a partial last page ended by erased flash, two blocks in one
 *          reply, malformed payloads, and an ACK with no log payload, which is
 *          how a tag signals end of log. It then checks the rows written.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled.
 *
 * @see     embedded/tags/families/PresTag/design/proposals/prestaghr.md
 */

// These are assertion programs: keep assert() live in Release builds too,
// where CMake defines NDEBUG and would otherwise compile every check out.
#undef NDEBUG

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <sqlite3.h>
#include <tag.pb.h>
#include "prestag_log_format.h"
#include "sqlitelog.h"

/**
 * @brief Store a raw 24-bit pressure and a raw temperature in a sample.
 *
 * @param[out] s           Sample to fill, in register byte order.
 * @param[in]  pressure    Signed raw pressure, 4096 LSB/hPa.
 * @param[in]  temperature Signed raw temperature, 100 LSB/degC.
 */
static void put(t_PresTagHRSample &s, int32_t pressure, int16_t temperature)
{
    const uint32_t u = static_cast<uint32_t>(pressure) & 0xFFFFFFu;
    s.pressure[0] = static_cast<uint8_t>(u);
    s.pressure[1] = static_cast<uint8_t>(u >> 8);
    s.pressure[2] = static_cast<uint8_t>(u >> 16);
    s.temperature = temperature;
}

/**
 * @brief Build a block whose page is fully erased (every byte 0xFF).
 *
 * @param[in] epoch Header epoch.
 * @return Block with the header filled and an erased page.
 */
static t_PresTagHRBlock block(int32_t epoch)
{
    t_PresTagHRBlock b;
    std::memset(&b, 0xFF, sizeof b);
    b.epoch = epoch;
    b.voltage = 2.5f;
    b.temperature = 2.5f;
    return b;
}

/**
 * @brief Write one reply of packed blocks.
 *
 * @param[in,out] writer  Open writer.
 * @param[in]     payload Packed t_PresTagHRBlock bytes.
 * @return writeLog()'s result: blocks consumed, 0 for no payload, -2 on error.
 */
static int writeReply(SqliteTagLogWriter &writer, const std::string &payload)
{
    Ack ack;
    ack.set_err(Ack_Err_OK);
    ack.mutable_prestag_hr_data_log()->set_samples(payload);
    return writer.writeLog(ack);
}

/**
 * @brief Run one scalar query.
 *
 * @param[in] db  Open database.
 * @param[in] sql Query returning one value.
 * @return The value, or NaN when there is no row or it is NULL.
 */
static double scalar(sqlite3 *db, const char *sql)
{
    double value = std::nan("");
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_type(st, 0) != SQLITE_NULL)
        value = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    return value;
}

/**
 * @brief Decode synthetic PresTagHR replies and check the database.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional argv[1]: output database path.
 * @return 0 when every block decoded as expected; asserts on any mismatch and
 *         returns 1 when the database cannot be opened.
 */
int main(int argc, char **argv)
{
    const char *db_path = (argc > 1) ? argv[1] : "prestaghr_decoder_check.db3";
    const int32_t e0 = 1767225600;
    const int32_t period = 20;
    std::remove(db_path);

    static_assert(sizeof(t_PresTagHRSample) == 5, "sample is 5 bytes");
    static_assert(sizeof(t_PresTagHRBlock) == 252, "block is 252 bytes");

    Config config;
    config.set_tag_type(PRESTAG);
    config.set_period(period);
    {
        SqliteTagLogWriter writer(db_path, config, true);
        if (!writer.isOpen() || !writer.beginLog()) {
            std::printf("FAIL: writer: %s\n", writer.lastError().c_str());
            return 1;
        }

        // Block A: a full page. Sample j is 1000 hPa plus j/4096 hPa, so the
        // low byte PresTag drops is what tells the samples apart. Sample 3
        // failed and must leave a gap, not a value. Sample 47 is negative in
        // both fields, to exercise sign extension.
        t_PresTagHRBlock a = block(e0);
        for (int j = 0; j < PRESTAG_HR_LOG_SAMPLES; j++)
            put(a.samples.data[j], 1000 * 4096 + j, static_cast<int16_t>(2000 + j));
        put(a.samples.data[3], PRESTAG_HR_PRESSURE_FAILED, INT16_MIN);
        put(a.samples.data[47], -4096, -550);

        // Block B: a partial page of 5 samples, then erased flash.
        t_PresTagHRBlock b = block(e0 + 3600);
        for (int j = 0; j < 5; j++)
            put(b.samples.data[j], 900 * 4096 + 1, 1500);

        std::string reply(reinterpret_cast<const char *>(&a), sizeof a);
        reply.append(reinterpret_cast<const char *>(&b), sizeof b);
        assert(writeReply(writer, reply) == 2);

        // Malformed: not a whole number of blocks, and empty.
        assert(writeReply(writer, reply.substr(0, sizeof a - 1)) < 0);
        assert(writeReply(writer, std::string()) < 0);

        // End of log: an ACK with no log payload.
        Ack empty;
        empty.set_err(Ack_Err_OK);
        assert(writer.writeLog(empty) == 0);

        if (!writer.endLog()) {
            std::printf("FAIL: endLog: %s\n", writer.lastError().c_str());
            return 1;
        }
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        std::printf("FAIL: cannot reopen %s\n", db_path);
        return 1;
    }
    // A: 48 - 1 failed = 47; B: 5.
    assert(scalar(db, "SELECT count(*) FROM Pressure") == 52);
    assert(scalar(db, "SELECT count(*) FROM Temperature") == 52);
    assert(scalar(db, "SELECT count(*) FROM Voltage") == 2);
    // The failed sample keeps its slot: nothing at e0 + 3 * period ...
    assert(std::isnan(scalar(db, "SELECT Pressure FROM Pressure WHERE Epoch = 1767225660")));
    // ... and sample 4 is still at e0 + 4 * period, carrying its low byte.
    assert(std::fabs(scalar(db, "SELECT Pressure FROM Pressure WHERE Epoch = 1767225680")
                     - (1000.0 + 4.0 / 4096.0)) < 1e-9);
    assert(std::fabs(scalar(db, "SELECT Temperature FROM Temperature WHERE Epoch = 1767225680")
                     - 20.04) < 1e-9);
    // Sign extension: sample 47 at e0 + 940.
    assert(std::fabs(scalar(db, "SELECT Pressure FROM Pressure WHERE Epoch = 1767226540")
                     + 1.0) < 1e-9);
    assert(std::fabs(scalar(db, "SELECT Temperature FROM Temperature WHERE Epoch = 1767226540")
                     + 5.5) < 1e-9);
    // Block B starts at its own header epoch and ends at the erased sample.
    assert(scalar(db, "SELECT min(Epoch) FROM Pressure WHERE Epoch >= 1767229200") == 1767229200);
    assert(scalar(db, "SELECT max(Epoch) FROM Pressure") == 1767229200 + 4 * period);
    assert(std::fabs(scalar(db, "SELECT Pressure FROM Pressure WHERE Epoch = 1767229200")
                     - (900.0 + 1.0 / 4096.0)) < 1e-9);
    sqlite3_close(db);

    std::printf("prestaghr_decoder_check: all assertions passed\n");
    return 0;
}
