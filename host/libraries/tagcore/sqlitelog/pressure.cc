#include "sqlitelog/internal.h"

#include <cstring>
#include <sstream>

#include "prestag_log_format.h"

namespace tagcore::sqlite_log {

// Pressure-family tags share pressure and temperature streams, with BitPresTag
// also unpacking compact activity buckets from each entry.
//
// PresTag and BitPresTag have similar environmental streams but different page
// pacing. Keep them in one file because their table schemas overlap; keep the
// two dump functions separate because their timestamp reconstruction differs.

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

    // PresTag entries are already ordered at config.period() spacing starting
    // at the header epoch.
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

int dumpPresTagRawLog(WriterContext &ctx, const PresTagRawLog &log)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    if (ctx.config.period() == 0) {
        ctx.setLastError("PresTag raw SQLite output requires a nonzero sample period");
        return -2;
    }

    if (log.pres_constant() == 0.0f || log.temp_constant() == 0.0f) {
        ctx.setLastError("PresTag raw SQLite output requires nonzero conversion constants");
        return -2;
    }

    Statement voltage_insert(ctx.db, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
    Statement pressure_insert(ctx.db, "INSERT INTO Pressure (Epoch, Pressure) VALUES (?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO Temperature (Epoch, Temperature) VALUES (?, ?)");

    if (!voltage_insert.valid()
        || !pressure_insert.valid()
        || !temperature_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare PresTag raw log insert");
        return -2;
    }

    const std::string &payload = log.samples();
    if ((payload.size() % sizeof(t_PresTagRawBlock)) != 0) {
        std::ostringstream error;
        error << "PresTag raw payload has " << payload.size()
              << " bytes, expected a multiple of " << sizeof(t_PresTagRawBlock);
        ctx.setLastError(error.str());
        return -2;
    }

    const size_t block_count = payload.size() / sizeof(t_PresTagRawBlock);
    for (size_t block_index = 0; block_index < block_count; block_index++) {
        t_PresTagRawBlock raw_block = {};
        std::memcpy(&raw_block,
                    payload.data() + (block_index * sizeof(raw_block)),
                    sizeof(raw_block));

        sqlite3_int64 timestamp = raw_block.epoch;

        if (!voltage_insert.bindInt64(1, timestamp)
            || !voltage_insert.bindDouble(2, raw_block.voltage)
            || !voltage_insert.stepDone()) {
            ctx.setLastSqliteError("PresTag raw log header insert failed");
            return -2;
        }

        for (const t_PresTagRawSample &sample : raw_block.samples.data) {
            if (sample.pressure == PRESTAG_RAW_PRESSURE_END) {
                break;
            }

            if (!pressure_insert.bindInt64(1, timestamp)
                || !pressure_insert.bindDouble(2,
                                               sample.pressure * log.pres_constant())
                || !pressure_insert.stepDone()
                || !temperature_insert.bindInt64(1, timestamp)
                || !temperature_insert.bindDouble(2,
                                                  sample.temperature * log.temp_constant())
                || !temperature_insert.stepDone()) {
                ctx.setLastSqliteError("PresTag raw log data insert failed");
                return -2;
            }

            timestamp += ctx.config.period();
        }
    }

    return static_cast<int>(block_count);
}

} // namespace tagcore::sqlite_log
