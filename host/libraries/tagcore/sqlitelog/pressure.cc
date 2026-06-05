#include "sqlitelog/internal.h"

namespace tagcore::sqlite_log {

// Pressure-family tags share pressure and temperature streams, with BitPresTag
// also unpacking compact activity buckets from each entry.

int dumpBitPresTagLog(WriterContext &ctx, const BitPresTagLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    constexpr int bucket_number = 4;
    constexpr int bucket_bits = 4;
    constexpr int bucket_period = 15;

    sqlite3_int64 timestamp = log.epoch();

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
        ctx.setLastSqliteError("Could not prepare BitPresTag log insert");
        return -2;
    }

    if (!voltage_insert.bindInt64(1, timestamp)
        || !voltage_insert.bindDouble(2, log.voltage())
        || !voltage_insert.stepDone()) {
        ctx.setLastSqliteError("BitPresTag log header insert failed");
        return -2;
    }

    for (auto const &entry : log.data()) {
        timestamp += bucket_period;

        const uint32_t raw_activity = entry.activity();
        for (int i = 0; i < bucket_number; i++) {
            const uint32_t count =
                (raw_activity >> (i * bucket_bits)) & ((1U << bucket_bits) - 1U);
            const double activity = count * 100.0 / bucket_period;

            if (!activity_insert.bindInt64(1, timestamp)
                || !activity_insert.bindDouble(2, activity)
                || !activity_insert.stepDone()) {
                ctx.setLastSqliteError("BitPresTag activity insert failed");
                return -2;
            }
        }

        if (!pressure_insert.bindInt64(1, timestamp)
            || !pressure_insert.bindDouble(2, entry.pressure())
            || !pressure_insert.stepDone()
            || !temperature_insert.bindInt64(1, timestamp)
            || !temperature_insert.bindDouble(2, entry.temperature())
            || !temperature_insert.stepDone()) {
            ctx.setLastSqliteError("BitPresTag pressure/temperature insert failed");
            return -2;
        }
    }

    return 1;
}

int dumpPresTagLog(WriterContext &ctx, const PresTagLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    if (ctx.config.period() == 0) {
        ctx.setLastError("PresTag SQLite output requires a nonzero sample period");
        return -2;
    }

    sqlite3_int64 timestamp = log.epoch();

    Statement voltage_insert(ctx.db, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
    Statement pressure_insert(ctx.db, "INSERT INTO Pressure (Epoch, Pressure) VALUES (?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO Temperature (Epoch, Temperature) VALUES (?, ?)");

    if (!voltage_insert.valid()
        || !pressure_insert.valid()
        || !temperature_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare PresTag log insert");
        return -2;
    }

    if (!voltage_insert.bindInt64(1, timestamp)
        || !voltage_insert.bindDouble(2, log.voltage())
        || !voltage_insert.stepDone()) {
        ctx.setLastSqliteError("PresTag log header insert failed");
        return -2;
    }

    for (auto const &entry : log.data()) {
        if (!pressure_insert.bindInt64(1, timestamp)
            || !pressure_insert.bindDouble(2, entry.pressure())
            || !pressure_insert.stepDone()
            || !temperature_insert.bindInt64(1, timestamp)
            || !temperature_insert.bindDouble(2, entry.temperature())
            || !temperature_insert.stepDone()) {
            ctx.setLastSqliteError("PresTag log data insert failed");
            return -2;
        }

        timestamp += ctx.config.period();
    }

    return 1;
}

} // namespace tagcore::sqlite_log
