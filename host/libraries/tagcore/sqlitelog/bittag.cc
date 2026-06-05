#include "sqlitelog/internal.h"

namespace tagcore::sqlite_log {

// BitTag logs pack activity buckets into each protobuf entry. The configured
// log format determines how many buckets are unpacked and their spacing.
//
// The voltage and core-temperature values are one row per protobuf entry. The
// activity value is a compact bitfield/counter history ending at entry.epoch(),
// so this decoder expands it into individual rows with reconstructed timestamps.

int dumpBitTagLog(WriterContext &ctx, const BitTagLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    int bucket_bits = 0;
    int bucket_number = 0;
    int bucket_period = 0;

    switch (ctx.config.bittag_log()) {
    case BITTAG_BITPERSEC:
        break;
    case BITTAG_BITSPERMIN:
        bucket_bits = 6;
        bucket_number = 10;
        bucket_period = 60;
        break;
    case BITTAG_BITSPERFOURMIN:
        bucket_bits = 8;
        bucket_number = 8;
        bucket_period = 60 * 4;
        break;
    case BITTAG_BITSPERFIVEMIN:
        bucket_bits = 9;
        bucket_number = 7;
        bucket_period = 60 * 5;
        break;
    default:
        ctx.setLastError("BitTag SQLite output requires a configured log format");
        return -2;
    }

    Statement voltage_insert(ctx.db, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO CoreTemperature (Epoch, Temperature) VALUES (?, ?)");
    Statement activity_insert(ctx.db, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");

    if (!voltage_insert.valid()
        || !temperature_insert.valid()
        || !activity_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare BitTag log insert");
        return -2;
    }

    for (auto const &entry : log.data()) {
        const sqlite3_int64 timestamp = entry.epoch();

        if (!voltage_insert.bindInt64(1, timestamp)
            || !voltage_insert.bindDouble(2, entry.voltage())
            || !voltage_insert.stepDone()
            || !temperature_insert.bindInt64(1, timestamp)
            || !temperature_insert.bindDouble(2, entry.temperature())
            || !temperature_insert.stepDone()) {
            ctx.setLastSqliteError("BitTag log header insert failed");
            return -2;
        }

        const uint64_t rawdata = entry.rawdata();
        if (ctx.config.bittag_log() == BITTAG_BITPERSEC) {
            for (int i = 0; i < 60; i++) {
                const sqlite3_int64 sample_timestamp = timestamp - (59 - i);
                const double activity = ((rawdata >> i) & 1) * 100.0;
                if (!activity_insert.bindInt64(1, sample_timestamp)
                    || !activity_insert.bindDouble(2, activity)
                    || !activity_insert.stepDone()) {
                    ctx.setLastSqliteError("BitTag activity insert failed");
                    return -2;
                }
            }
        } else {
            for (int i = 0; i < bucket_number; i++) {
                const sqlite3_int64 sample_timestamp =
                    timestamp - bucket_period * (bucket_number - 1 - i);
                const uint64_t count =
                    (rawdata >> (i * bucket_bits)) & ((1 << bucket_bits) - 1);
                const double activity = count * 100.0 / bucket_period;

                if (!activity_insert.bindInt64(1, sample_timestamp)
                    || !activity_insert.bindDouble(2, activity)
                    || !activity_insert.stepDone()) {
                    ctx.setLastSqliteError("BitTag activity insert failed");
                    return -2;
                }
            }
        }
    }

    return log.data().size();
}

} // namespace tagcore::sqlite_log
