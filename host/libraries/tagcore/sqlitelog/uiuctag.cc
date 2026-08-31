/**
 * @file    uiuctag.cc
 * @brief   SQLite decoder for UIUCTag packed pressure/activity log blocks.
 *
 * @details UIUCTag downloads carry one two-hour block per ACK: up to 24 packed
 *          twelve-byte samples, each holding float pressure, float temperature,
 *          and five six-bit activity counts covering the five minutes that
 *          follow the sample. Unlike the other decoders in this directory,
 *          which unpack decoded protobuf record lists, the payload here is a
 *          byte image of t_UIUCTagSample[] copied straight out of external
 *          flash.
 *
 *          Every layout and decode rule therefore comes from the shared
 *          include/uiuctag_log_format.h, which the firmware includes verbatim,
 *          rather than from constants repeated on this side. That sharing is
 *          deliberate: the two hard-coded and mutually inconsistent copies of
 *          the BitPresTag activity geometry, in pressure.cc and txtlogs.cc, are
 *          what it exists to avoid.
 *
 *          Rows land in the same tables as BitPresTag - Voltage, Pressure,
 *          Temperature, Activity - so viewers and analysis queries are
 *          identical across the two tags.
 *
 * @note    Absent values produce no row at all. See dumpUIUCTagLog() for why.
 *
 * @see     sqlitelog/README.md, section "UIUCTag Downloader Fields"
 * @see     embedded/tags/families/BitPresTag/design/uiuctag-data-collection.md
 */

#include "sqlitelog/internal.h"

#include <cstdint>
#include <cstring>
#include <sstream>

#include "uiuctag_log_format.h"

namespace tagcore::sqlite_log {

namespace {

constexpr size_t kSampleBytes = sizeof(t_UIUCTagSample);
constexpr size_t kMaxBlockBytes = UIUCTAG_SAMPLE_BYTES_MAX;
constexpr unsigned kBucketsPerSample =
    UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK;
constexpr double kBucketSeconds = UIUCTAG_ACTIVITY_BUCKET_SECONDS;

static_assert(kSampleBytes == UIUCTAG_SAMPLE_SIZE,
              "UIUCTag host decoder expects the shared sample layout");
static_assert(kMaxBlockBytes == kSampleBytes * UIUCTAG_LOG_SAMPLES,
              "UIUCTag block size must match the shared sample count");

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
 * @details Writes one Voltage row per block at the raw checkpoint epoch, then
 *          per sample slot a Pressure row, a Temperature row, and five Activity
 *          rows, at epochs derived from the block window boundary. The payload
 *          index is the slot number: the firmware always sends a block from
 *          slot 0 and trims only trailing unwritten slots, so a short payload
 *          is a valid partial block rather than a shifted one.
 *
 *          Absent values produce no row. A slot may be empty because no wake
 *          ever filled it, which reads as erased flash, or because the
 *          conversion failed, which stores a canonical NaN; the log cannot
 *          always tell those apart, but either way inserting a row would invent
 *          a measurement. Skipping keeps gaps visible in plots and out of
 *          aggregates, which storing NaN or zero would not.
 *
 * @param[in,out] ctx Writer bridge supplying the database handle, the log-table
 *                    constructor, and the error reporters.
 * @param[in] log One downloaded block: checkpoint epoch, voltage, and the
 *                packed sample image.
 * @return  1 when the block was consumed, following the TagLogWriter
 *          records-consumed convention, or -2 on a malformed payload, a table
 *          creation failure, or any SQLite error. A payload whose length is not
 *          a whole number of samples, or is longer than one block, is rejected
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

    if (payload.size() > kMaxBlockBytes || (payload.size() % kSampleBytes) != 0) {
        std::ostringstream error;
        error << "UIUCTag block has " << payload.size()
              << " bytes, expected a multiple of " << kSampleBytes
              << " up to " << kMaxBlockBytes;
        ctx.setLastError(error.str());
        return -2;
    }

    const size_t sample_count = payload.size() / kSampleBytes;
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

    // The voltage reading is taken as the block opens, so it belongs at the
    // checkpoint's own timestamp rather than at the block window boundary.
    if (!voltage_insert.bindInt64(1, header_epoch)
        || !voltage_insert.bindDouble(2, log.voltage())
        || !voltage_insert.stepDone()) {
        ctx.setLastSqliteError("UIUCTag log header insert failed");
        return -2;
    }

    for (size_t slot = 0; slot < sample_count; slot++) {
        // Copy rather than cast: protobuf bytes storage carries no alignment
        // guarantee and t_UIUCTagSample is packed.
        t_UIUCTagSample sample;
        std::memcpy(&sample, payload.data() + (slot * kSampleBytes), kSampleBytes);

        const sqlite3_int64 sample_epoch =
            uiuctagSampleEpoch(header_epoch, static_cast<unsigned>(slot));

        if (uiuctagSampleHasPressure(&sample)) {
            if (!pressure_insert.bindInt64(1, sample_epoch)
                || !pressure_insert.bindDouble(2, sample.pressure)
                || !pressure_insert.stepDone()) {
                ctx.setLastSqliteError("UIUCTag pressure insert failed");
                return -2;
            }
        }

        if (uiuctagSampleHasTemperature(&sample)) {
            if (!temperature_insert.bindInt64(1, sample_epoch)
                || !temperature_insert.bindDouble(2, sample.temperature)
                || !temperature_insert.stepDone()) {
                ctx.setLastSqliteError("UIUCTag temperature insert failed");
                return -2;
            }
        }

        // The activity word for a slot is written one sample period after its
        // pressure, so the newest slot of a live capture legitimately has none.
        if (!uiuctagSampleHasActivity(&sample)) {
            continue;
        }

        for (unsigned bucket = 0; bucket < kBucketsPerSample; bucket++) {
            const sqlite3_int64 bucket_epoch = uiuctagActivityBucketEpoch(
                header_epoch, static_cast<unsigned>(slot), bucket);
            const uint32_t active_seconds = uiuctagActivityBucket(&sample, bucket);

            if (!activity_insert.bindInt64(1, bucket_epoch)
                || !activity_insert.bindDouble(2, activityPercent(active_seconds))
                || !activity_insert.stepDone()) {
                ctx.setLastSqliteError("UIUCTag activity insert failed");
                return -2;
            }
        }
    }

    return 1;
}

} // namespace tagcore::sqlite_log
