/**
 * @file    uiuctag_decoder_check.cc
 * @brief   Drives the UIUCTag SQLite decoder with synthetic download blocks.
 *
 * @details Builds UIUCTagLog messages by hand and runs them through
 *          SqliteTagLogWriter, so the decoder can be developed and regression
 *          checked with no tag attached. The synthetic blocks cover what real
 *          captures produce only rarely or late: a full block, a partial block,
 *          an entirely erased block, an interior failed conversion, an activity
 *          ramp touching both the 0 and 60 second edges, a malformed payload
 *          length, and an ACK carrying no log payload at all, which is how the
 *          tag signals end of log.
 *
 *          Row-level expectations are checked by querying the resulting
 *          database; see README.md for the queries and what they should return.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled.
 *
 * @see     embedded/tags/families/BitPresTag/design/uiuctag-test-strategy.md
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>

#include <tag.pb.h>
#include "sqlitelog.h"
#include "uiuctag_log_format.h"

/**
 * @brief Build a sample slot in the erased state.
 *
 * @return Sample with every byte set to 0xFF, as unwritten NOR flash reads.
 */
static t_UIUCTagSample erasedSample()
{
    t_UIUCTagSample s;
    std::memset(&s, 0xFF, sizeof(s));
    return s;
}

/**
 * @brief Serialize samples into a download payload.
 *
 * @param[in] samples Slots in block order, starting at slot 0.
 * @return Byte image suitable for UIUCTagLog::set_samples().
 */
static std::string pack(const std::vector<t_UIUCTagSample> &samples)
{
    std::string out;
    out.resize(samples.size() * sizeof(t_UIUCTagSample));
    if (!samples.empty()) {
        std::memcpy(out.data(), samples.data(), out.size());
    }
    return out;
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
    const int32_t block0 = 1767225600;            // multiple of 7200
    Config config;
    config.set_tag_type(UIUCTAG);

    SqliteTagLogWriter writer(db_path, config, true);
    if (!writer.isOpen()) {
        printf("FAIL: writer not open: %s\n", writer.lastError().c_str());
        return 1;
    }
    if (!writer.beginLog()) {
        printf("FAIL beginLog: %s\n", writer.lastError().c_str());
        return 1;
    }

    // --- Block A: full 24 samples, activity ramp, one interior failed sample
    {
        std::vector<t_UIUCTagSample> samples;
        for (unsigned i = 0; i < UIUCTAG_LOG_SAMPLES; i++) {
            t_UIUCTagSample s = erasedSample();
            s.pressure = 1000.0f + i;
            s.temperature = 20.0f + i * 0.5f;
            s.packed_activity_data = 0;
            for (unsigned b = 0; b < 5; b++) {
                // bucket counts 0,15,30,45,60 -> exercises both edges
                s.packed_activity_data |= (uint32_t)(b * 15) << (b * 6);
            }
            if (i == 7) {                          // conversion failure
                s.pressure = __builtin_nanf("");
                s.temperature = __builtin_nanf("");
            }
            if (i == 11) {                         // activity never written
                s.packed_activity_data = UIUCTAG_ERASED_WORD;
            }
            samples.push_back(s);
        }
        Ack ack;
        auto *log = ack.mutable_uiuctag_data_log();
        log->set_epoch(block0);
        log->set_voltage(3.01f);
        log->set_samples(pack(samples));
        int rc = writer.writeLog(ack);
        printf("block A rc=%d (%s)\n", rc, writer.lastError().c_str());
        assert(rc == 1);
    }

    // --- Block B: partial (5 samples), opened late at slot 3
    {
        std::vector<t_UIUCTagSample> samples;
        for (unsigned i = 0; i < 5; i++) {
            t_UIUCTagSample s = erasedSample();
            s.pressure = 900.0f + i;
            s.temperature = 10.0f;
            s.packed_activity_data = 60u;          // bucket0=60, rest 0
            samples.push_back(s);
        }
        Ack ack;
        auto *log = ack.mutable_uiuctag_data_log();
        log->set_epoch(block0 + 7200 + 900);       // late open, slot 3
        log->set_voltage(2.95f);
        log->set_samples(pack(samples));
        int rc = writer.writeLog(ack);
        printf("block B rc=%d\n", rc);
        assert(rc == 1);
    }

    // --- Block C: entirely erased block
    {
        std::vector<t_UIUCTagSample> samples(UIUCTAG_LOG_SAMPLES, erasedSample());
        Ack ack;
        auto *log = ack.mutable_uiuctag_data_log();
        log->set_epoch(block0 + 14400);
        log->set_voltage(2.90f);
        log->set_samples(pack(samples));
        int rc = writer.writeLog(ack);
        printf("block C rc=%d\n", rc);
        assert(rc == 1);
    }

    // --- Block D: malformed payload (not a multiple of the sample size)
    {
        Ack ack;
        auto *log = ack.mutable_uiuctag_data_log();
        log->set_epoch(block0 + 21600);
        log->set_voltage(2.90f);
        log->set_samples(std::string(13, '\0'));
        int rc = writer.writeLog(ack);
        printf("block D rc=%d err=\"%s\"\n", rc, writer.lastError().c_str());
        assert(rc == -2);
    }

    // --- Ack with no UIUCTag payload = end of log, not an error
    {
        Ack ack;
        int rc = writer.writeLog(ack);
        printf("empty ack rc=%d\n", rc);
        assert(rc == 0);
    }

    if (!writer.endLog()) {
        printf("FAIL endLog: %s\n", writer.lastError().c_str());
        return 1;
    }
    printf("harness done\n");
    return 0;
}
