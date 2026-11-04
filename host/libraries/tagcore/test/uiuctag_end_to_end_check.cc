/**
 * @file    uiuctag_end_to_end_check.cc
 * @brief   Decodes firmware-simulated flash images through the SQLite writer.
 *
 * @details Closes the loop between the two halves of the UIUCTag log path
 *          without hardware. It reads the flash images written by the firmware
 *          sequencer simulation, applies the same trailing-trim rule
 *          data_logAck() applies, wraps them as UIUCTagLog messages, and decodes
 *          them with SqliteTagLogWriter.
 *
 *          What this catches that neither side catches alone is disagreement
 *          about the contract: record-to-epoch mapping, whether the payload is
 *          sent from record 0, how a partial block is trimmed, and whether a
 *          value the firmware considers absent is a value the host omits. It
 *          also checks every decoded block against the simulation's run: a
 *          reading every minute, and three activity rows for every record
 *          whose minute was observed.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled. Requires the block
 *          file produced by embedded/tags/UIUCTag/test/sequencer_sim.c.
 *
 * @see     embedded/tags/UIUCTag/design/test-strategy.md
 */

// These are assertion programs: keep assert() live in Release builds too,
// where CMake defines NDEBUG and would otherwise compile every check out.
#undef NDEBUG

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>

#include <tag.pb.h>
#include "sqlitelog.h"
#include "uiuctag_log_format.h"

/**
 * @brief Decode simulated firmware blocks into a SQLite log.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional argv[1]: block file from the firmware sequencer
 *                 simulation. Optional argv[2]: output database path.
 * @return 0 when every block decoded, 1 when the block file is missing or a
 *         block was rejected.
 */
int main(int argc, char **argv)
{
    const char *blocks_path = (argc > 1) ? argv[1] : "uiuc_sim_blocks.bin";
    const char *db_path = (argc > 2) ? argv[2] : "uiuctag_end_to_end.db3";

    std::ifstream in(blocks_path, std::ios::binary);
    if (!in) {
        printf("FAIL: cannot open %s (run sequencer_sim first)\n", blocks_path);
        return 1;
    }

    Config config;
    config.set_tag_type(UIUCTAG);
    SqliteTagLogWriter writer(db_path, config, true);
    if (!writer.isOpen() || !writer.beginLog()) {
        printf("FAIL open: %s\n", writer.lastError().c_str());
        return 1;
    }

    int blocks = 0;
    int failed_readings = 0;
    for (;;) {
        int32_t epoch;
        uint16_t vdd100;
        std::vector<char> image(UIUCTAG_BLOCK_BYTES);

        if (!in.read(reinterpret_cast<char *>(&epoch), sizeof(epoch))) break;
        in.read(reinterpret_cast<char *>(&vdd100), sizeof(vdd100));
        in.read(image.data(), image.size());

        // Firmware-side trailing trim, as in data_logAck().
        size_t used = image.size();
        while (used >= UIUCTAG_RECORD_SIZE) {
            t_UIUCTagRecord r;
            std::memcpy(&r, image.data() + used - UIUCTAG_RECORD_SIZE, sizeof(r));
            if (!uiuctagRecordErased(&r)) break;
            used -= UIUCTAG_RECORD_SIZE;
        }

        // Every record the firmware wrote is complete: its activity word is
        // always written, observed or not (the reading may be the failure
        // value, which the simulation produces once on purpose).
        for (size_t at = 0; at < used; at += UIUCTAG_RECORD_SIZE) {
            t_UIUCTagRecord r;
            std::memcpy(&r, image.data() + at, sizeof(r));
            if (uiuctagRecordErased(&r)) continue;
            assert(uiuctagRecordHasActivity(&r));
            failed_readings += uiuctagRecordHasReading(&r) ? 0 : 1;
        }

        Ack ack;
        auto *log = ack.mutable_uiuctag_data_log();
        log->set_epoch(epoch);
        log->set_voltage(vdd100 * 0.01f);
        log->set_samples(std::string(image.data(), used));

        int rc = writer.writeLog(ack);
        printf("block %d: epoch=%d payload=%zu bytes (%zu records) rc=%d\n",
               blocks, epoch, used, used / UIUCTAG_RECORD_SIZE, rc);
        if (rc != 1) { printf("FAIL rc=%d %s\n", rc, writer.lastError().c_str()); return 1; }
        blocks++;
    }

    writer.endLog();
    printf("decoded %d blocks, %d failed reading(s)\n", blocks, failed_readings);
    // sequencer_sim.c fails exactly one reading, at minute 7.
    assert(failed_readings == 1);
    return 0;
}
