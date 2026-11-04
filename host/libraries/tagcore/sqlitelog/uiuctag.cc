/**
 * @file    uiuctag.cc
 * @brief   SQLite decoder for UIUCTag minute-record log blocks.
 *
 * @details UIUCTag downloads carry one 32-minute block per ACK: up to 32
 *          8-byte records, each a raw BMP585 pressure and temperature reading
 *          and three 20-second activity counts for the minute before it.
 *          Unlike the other decoders in this directory, which unpack decoded
 *          protobuf record lists, the payload here is a byte image of
 *          t_UIUCTagRecord[] copied straight out of external flash.
 *
 *          Every layout, conversion and decode rule therefore comes from the
 *          shared include/uiuctag_log_format.h, which the firmware includes
 *          verbatim, rather than from constants repeated on this side. That
 *          sharing is deliberate: the two hard-coded and mutually inconsistent
 *          copies of the BitPresTag activity geometry, in pressure.cc and
 *          txtlogs.cc, are what it exists to avoid.
 *
 *          Rows land in the same tables as BitPresTag - Voltage, Pressure,
 *          Temperature, Activity - so viewers and analysis queries are
 *          identical across the two tags.
 *
 * @note    Absent values produce no row at all. See dumpUIUCTagLog() for why.
 *
 * @see     sqlitelog/README.md, section "UIUCTag Downloader Fields"
 * @see     embedded/tags/UIUCTag/design/overview.md
 */

#include "sqlitelog/internal.h"

#include <cstdint>
#include <cstring>
#include <sstream>

#include "uiuctag_log_format.h"

namespace tagcore::sqlite_log {

namespace {

constexpr size_t kRecordBytes = sizeof(t_UIUCTagRecord);
constexpr size_t kMaxBlockBytes = UIUCTAG_BLOCK_BYTES;
constexpr double kBucketSeconds = UIUCTAG_ACTIVITY_BUCKET_SECONDS;

static_assert(kRecordBytes == UIUCTAG_RECORD_SIZE,
              "UIUCTag host decoder expects the shared record layout");
static_assert(kMaxBlockBytes == kRecordBytes * UIUCTAG_LOG_RECORDS,
              "UIUCTag block size must match the shared record count");

/**
 * @brief   Convert a bucket's active-second count to percent occupancy.
 *
 * @details Percent is what the Activity stream metadata in schema.cc declares,
 *          and what the BitPresTag and CompassTag decoders emit, so activity
 *          from different tag families stays directly comparable despite their
 *          different bucket periods.
 *
 * @param[in] active_seconds Active seconds in one bucket, 0 to the bucket
 *                           period.
 * @return  Occupancy of the bucket as a percentage, 0.0 to 100.0.
 */
double activityPercent(uint32_t active_seconds)
{
    return active_seconds * 100.0 / kBucketSeconds;
}

} // namespace

/**
 * @brief   Unpack one downloaded UIUCTag block into SQLite rows.
 *
 * @details Writes one Voltage row per block at the checkpoint epoch, then per
 *          record a Pressure and a Temperature row at the record's epoch and
 *          up to three Activity rows at the starts of the 20-second buckets of
 *          the minute before it. The payload index is the slot number: the
 *          firmware always sends a block from record 0 and trims only trailing
 *          unwritten records, so a short payload is a valid partial block
 *          rather than a shifted one.
 *
 *          Absent values produce no row: an erased record, a failed reading,
 *          and an activity bucket the tag did not observe. Inserting a row
 *          would invent a measurement; skipping keeps gaps visible in plots
 *          and out of aggregates, which storing NaN or zero would not.
 *
 * @param[in,out] ctx Writer bridge supplying the database handle, the log-table
 *                    constructor, and the error reporters.
 * @param[in] log One downloaded block: checkpoint epoch, voltage, and the
 *                block's records as stored.
 * @return  1 when the block was consumed, following the TagLogWriter
 *          records-consumed convention, or -2 on a malformed payload, a table
 *          creation failure, or any SQLite error. A payload whose length is not
 *          a whole number of records, or is longer than one block, is rejected
 *          rather than partially decoded.
 *
 * @post    On any error return, ctx.setLastError() or ctx.setLastSqliteError()
 *          has been called, so SqliteTagLogWriter::lastError() stays useful to
 *          CLI and Qt callers.
 */
int dumpUIUCTagLog(WriterContext &ctx, const UIUCTagLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    const std::string &payload = log.samples();

    if (payload.size() > kMaxBlockBytes || (payload.size() % kRecordBytes) != 0) {
        std::ostringstream error;
        error << "UIUCTag block has " << payload.size()
              << " bytes, expected a multiple of " << kRecordBytes
              << " up to " << kMaxBlockBytes;
        ctx.setLastError(error.str());
        return -2;
    }

    const size_t record_count = payload.size() / kRecordBytes;
    const int32_t header_epoch = log.epoch();

    Statement voltage_insert(ctx.db, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
    Statement activity_insert(ctx.db, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");
    Statement pressure_insert(ctx.db, "INSERT INTO Pressure (Epoch, Pressure) VALUES (?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO Temperature (Epoch, Temperature) VALUES (?, ?)");

    if (!voltage_insert.valid()
        || !activity_insert.valid()
        || !pressure_insert.valid()
        || !temperature_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare UIUCTag log insert");
        return -2;
    }

    // The voltage reading is taken as the block opens, which is also the time
    // of its record 0, so the header epoch is the right timestamp for it.
    if (!voltage_insert.bindInt64(1, header_epoch)
        || !voltage_insert.bindDouble(2, log.voltage())
        || !voltage_insert.stepDone()) {
        ctx.setLastSqliteError("UIUCTag log header insert failed");
        return -2;
    }

    for (size_t slot = 0; slot < record_count; slot++) {
        // Copy rather than cast: protobuf bytes storage carries no alignment
        // guarantee and t_UIUCTagRecord is packed.
        t_UIUCTagRecord record;
        std::memcpy(&record, payload.data() + (slot * kRecordBytes), kRecordBytes);

        if (uiuctagRecordErased(&record)) {
            continue;
        }

        for (unsigned bucket = 0; bucket < UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD;
             bucket++) {
            const uint32_t active_seconds = uiuctagActivityBucket(&record, bucket);
            if (active_seconds == UIUCTAG_ACTIVITY_NOT_OBSERVED) {
                continue;
            }
            const sqlite3_int64 bucket_epoch = uiuctagActivityBucketEpoch(
                header_epoch, static_cast<unsigned>(slot), bucket);

            if (!activity_insert.bindInt64(1, bucket_epoch)
                || !activity_insert.bindDouble(2, activityPercent(active_seconds))
                || !activity_insert.stepDone()) {
                ctx.setLastSqliteError("UIUCTag activity insert failed");
                return -2;
            }
        }

        if (!uiuctagRecordHasReading(&record)) {
            continue;
        }

        const sqlite3_int64 record_epoch =
            uiuctagRecordEpoch(header_epoch, static_cast<unsigned>(slot));

        if (!pressure_insert.bindInt64(1, record_epoch)
            || !pressure_insert.bindDouble(2, uiuctagRecordPressureHpa(&record))
            || !pressure_insert.stepDone()) {
            ctx.setLastSqliteError("UIUCTag pressure insert failed");
            return -2;
        }

        if (!temperature_insert.bindInt64(1, record_epoch)
            || !temperature_insert.bindDouble(2, uiuctagRecordTemperatureC(&record))
            || !temperature_insert.stepDone()) {
            ctx.setLastSqliteError("UIUCTag temperature insert failed");
            return -2;
        }
    }

    return 1;
}

} // namespace tagcore::sqlite_log
