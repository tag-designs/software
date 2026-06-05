#include "sqlitelog/internal.h"

namespace tagcore::sqlite_log {

// CompassTag packets contain one header timestamp followed by 15 second
// activity and compass samples.

int dumpCompassTagLog(WriterContext &ctx, const CompassTagLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    sqlite3_int64 timestamp = log.epoch();

    Statement voltage_insert(ctx.db, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO CoreTemperature (Epoch, Temperature) VALUES (?, ?)");
    Statement activity_insert(ctx.db, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");
    Statement compass_insert(
        ctx.db,
        "INSERT INTO Compass (Epoch, ax, ay, az, mx, my, mz) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");

    if (!voltage_insert.valid()
        || !temperature_insert.valid()
        || !activity_insert.valid()
        || !compass_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare log insert");
        return -2;
    }

    if (!voltage_insert.bindInt64(1, timestamp)
        || !voltage_insert.bindDouble(2, log.voltage())
        || !voltage_insert.stepDone()
        || !temperature_insert.bindInt64(1, timestamp)
        || !temperature_insert.bindDouble(2, log.temperature())
        || !temperature_insert.stepDone()) {
        ctx.setLastSqliteError("Log header insert failed");
        return -2;
    }

    for (auto const &entry : log.data()) {
        timestamp += 15;

        if (!activity_insert.bindInt64(1, timestamp)
            || !activity_insert.bindDouble(2, entry.activity())
            || !activity_insert.stepDone()
            || !compass_insert.bindInt64(1, timestamp)
            || !compass_insert.bindDouble(2, entry.ax())
            || !compass_insert.bindDouble(3, entry.ay())
            || !compass_insert.bindDouble(4, entry.az())
            || !compass_insert.bindDouble(5, entry.mx())
            || !compass_insert.bindDouble(6, entry.my())
            || !compass_insert.bindDouble(7, entry.mz())
            || !compass_insert.stepDone()) {
            ctx.setLastSqliteError("Log data insert failed");
            return -2;
        }
    }

    return 1;
}

} // namespace tagcore::sqlite_log
