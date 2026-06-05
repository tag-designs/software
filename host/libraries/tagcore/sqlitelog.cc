#include "sqlitelog.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <sqlite3.h>
#include <string>
#include <vector>

#include <google/protobuf/util/json_util.h>
#include <tagclass.h>

extern "C"
{
#include "log.h"
}

namespace
{

// Small RAII wrapper for prepared statements. SQLite statements must be
// finalized even on early returns, and repeated inserts need reset/clear after
// each SQLITE_DONE so bindings from one row cannot leak into the next row.
class Statement
{
public:
    Statement(sqlite3 *db, const char *sql) : db_(db)
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }

    ~Statement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    bool valid() const
    {
        return stmt_ != nullptr;
    }

    bool bindInt64(int index, sqlite3_int64 value)
    {
        return sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK;
    }

    bool bindDouble(int index, double value)
    {
        return sqlite3_bind_double(stmt_, index, value) == SQLITE_OK;
    }

    bool bindText(int index, const std::string &value)
    {
        return sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bindNull(int index)
    {
        return sqlite3_bind_null(stmt_, index) == SQLITE_OK;
    }

    bool stepDone()
    {
        const bool done = sqlite3_step(stmt_) == SQLITE_DONE;
        if (done) {
            // Keep prepared inserts reusable for loops over calibration,
            // states, and data samples.
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
        return done;
    }

private:
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
};

struct SqlColumnDefinition
{
    const char *name;
    const char *type;
};

struct SqlStreamDefinition
{
    const char *id;
    const char *group_id;
    const char *group_name;
    const char *table;
    const char *time_column;
    const char *value_column;
    const char *kind;
    const char *display_name;
    const char *units;
    const char *quantity;
    const char *comment;
};

struct SqlTableDefinition
{
    const char *name;
    std::vector<SqlColumnDefinition> columns;
    std::vector<SqlStreamDefinition> streams;
};

struct SqlTagProfile
{
    bool has_calibration_table;
    std::vector<SqlTableDefinition> tables;
};

SqlTableDefinition voltageTable();
SqlTableDefinition coreTemperatureTable();
SqlTableDefinition activityTable();
SqlTableDefinition pressureTable();
SqlTableDefinition sensorTemperatureTable();
SqlTableDefinition compassTable();
SqlTableDefinition imuHeaderTable();
SqlTableDefinition imuPressureTable();
SqlTableDefinition imuMagTable();
SqlTableDefinition imuAccelTable();
SqlTableDefinition imuGyroTable();

constexpr int kImuSamplesPerBlock = 10;
constexpr int kImuAxesPerSample = 6;
constexpr int kImuBytesPerSample = kImuAxesPerSample * 2;
constexpr int kImuRawDataBytes = kImuSamplesPerBlock * kImuBytesPerSample;
constexpr int kImuDataLogBytes = 128;
constexpr int kImuRawDataOffset = 8;

int imuOdrHz(Lsm6dsv_ODR odr)
{
    switch (odr) {
    case Lsm6dsv_ODR_S50:
        return 50;
    case Lsm6dsv_ODR_S100:
        return 100;
    case Lsm6dsv_ODR_S200:
        return 200;
    case Lsm6dsv_ODR_S400:
        return 400;
    case Lsm6dsv_ODR_S800:
        return 800;
    default:
        return 0;
    }
}

double imuAccelMgPerLsb(Lsm6dsv_ACCEL range)
{
    switch (range) {
    case Lsm6dsv_ACCEL_R2G:
        return 0.061;
    case Lsm6dsv_ACCEL_R4G:
        return 0.122;
    case Lsm6dsv_ACCEL_R8G:
        return 0.244;
    case Lsm6dsv_ACCEL_R16G:
        return 0.488;
    default:
        return 1.0;
    }
}

double imuGyroDpsPerLsb(Lsm6dsv_GYRO range)
{
    switch (range) {
    case Lsm6dsv_GYRO_R125dps:
        return 0.004375;
    case Lsm6dsv_GYRO_R250dps:
        return 0.00875;
    case Lsm6dsv_GYRO_R500dps:
        return 0.0175;
    case Lsm6dsv_GYRO_R1000dps:
        return 0.035;
    case Lsm6dsv_GYRO_R2000dps:
        return 0.07;
    case Lsm6dsv_GYRO_R4000dps:
        return 0.14;
    default:
        return 1.0;
    }
}

int16_t readLeI16(const uint8_t *p)
{
    const uint16_t value = static_cast<uint16_t>(p[0])
        | (static_cast<uint16_t>(p[1]) << 8);
    return static_cast<int16_t>(value);
}

// ---------------------------------------------------------------------------
// Schema Profiles
// ---------------------------------------------------------------------------
//
// This is the top-level schema switch for SQLite log output. Adding a new tag
// type should start here: choose which persisted tables it writes and whether it
// has calibration history. The table definitions below own the SQL columns and
// stream metadata for those tables.
SqlTagProfile sqliteProfileForTag(TagType tag_type)
{
    switch (tag_type) {
    case BITTAG:
        return {false, {voltageTable(), coreTemperatureTable(), activityTable()}};
    case COMPASSTAG:
        return {
            true,
            {voltageTable(), coreTemperatureTable(), activityTable(), compassTable()}};
    case PRESTAG:
        return {false, {voltageTable(), pressureTable(), sensorTemperatureTable()}};
    case BITPRESTAG:
        return {false, {voltageTable(), activityTable(), pressureTable(), sensorTemperatureTable()}};
    case IMUTAG:
        return {
            false,
            {imuHeaderTable(), imuPressureTable(), imuMagTable(), imuAccelTable(), imuGyroTable()}};
    default:
        return {false, {}};
    }
}

// ---------------------------------------------------------------------------
// Table Definitions
// ---------------------------------------------------------------------------
//
// The SQLite writer owns the persisted schema contract. Each table definition
// creates one SQLite table and emits the stream metadata that external tools can
// use to understand the table without reading sensorViz source code.
SqlTableDefinition voltageTable()
{
    return {
        "Voltage",
        {{"Epoch", "INTEGER"}, {"Voltage", "REAL"}},
        {{
            "voltage",
            nullptr,
            nullptr,
            "Voltage",
            "Epoch",
            "Voltage",
            "scalar",
            "Voltage",
            "V",
            "voltage",
            "Tag supply voltage.",
        }},
    };
}

SqlTableDefinition coreTemperatureTable()
{
    return {
        "CoreTemperature",
        {{"Epoch", "INTEGER"}, {"Temperature", "REAL"}},
        {{
            "core_temperature",
            nullptr,
            nullptr,
            "CoreTemperature",
            "Epoch",
            "Temperature",
            "scalar",
            "Core temperature",
            "C",
            "temperature",
            "Internal tag temperature. Pressure tags use the source field for another voltage measurement and do not write this stream.",
        }},
    };
}

SqlTableDefinition activityTable()
{
    return {
        "Activity",
        {{"Epoch", "INTEGER"}, {"Activity", "REAL"}},
        {{
            "activity",
            nullptr,
            nullptr,
            "Activity",
            "Epoch",
            "Activity",
            "scalar",
            "Activity",
            "%",
            "activity",
            "Percent activity over the sample interval.",
        }},
    };
}

SqlTableDefinition pressureTable()
{
    return {
        "Pressure",
        {{"Epoch", "INTEGER"}, {"Pressure", "REAL"}},
        {{
            "pressure",
            nullptr,
            nullptr,
            "Pressure",
            "Epoch",
            "Pressure",
            "scalar",
            "Pressure",
            "mbar",
            "pressure",
            "Absolute pressure from the pressure sensor.",
        }},
    };
}

SqlTableDefinition sensorTemperatureTable()
{
    return {
        "Temperature",
        {{"Epoch", "INTEGER"}, {"Temperature", "REAL"}},
        {{
            "sensor_temperature",
            nullptr,
            nullptr,
            "Temperature",
            "Epoch",
            "Temperature",
            "scalar",
            "Sensor temperature",
            "C",
            "temperature",
            "Temperature reported by the pressure sensor.",
        }},
    };
}

SqlTableDefinition compassTable()
{
    return {
        "Compass",
        {
            {"Epoch", "INTEGER"},
            {"ax", "REAL"},
            {"ay", "REAL"},
            {"az", "REAL"},
            {"mx", "REAL"},
            {"my", "REAL"},
            {"mz", "REAL"},
        },
        {
            {
                "compass_ax", "compass_raw", "Compass raw", "Compass", "Epoch", "ax",
                "record_column", "Acceleration X", "mg", "acceleration_x",
                "Raw accelerometer X sample used for compass orientation.",
            },
            {
                "compass_ay", "compass_raw", "Compass raw", "Compass", "Epoch", "ay",
                "record_column", "Acceleration Y", "mg", "acceleration_y",
                "Raw accelerometer Y sample used for compass orientation.",
            },
            {
                "compass_az", "compass_raw", "Compass raw", "Compass", "Epoch", "az",
                "record_column", "Acceleration Z", "mg", "acceleration_z",
                "Raw accelerometer Z sample used for compass orientation.",
            },
            {
                "compass_mx", "compass_raw", "Compass raw", "Compass", "Epoch", "mx",
                "record_column", "Magnetic field X", "uT", "magnetic_field_x",
                "Raw magnetometer X sample used for compass orientation.",
            },
            {
                "compass_my", "compass_raw", "Compass raw", "Compass", "Epoch", "my",
                "record_column", "Magnetic field Y", "uT", "magnetic_field_y",
                "Raw magnetometer Y sample used for compass orientation.",
            },
            {
                "compass_mz", "compass_raw", "Compass raw", "Compass", "Epoch", "mz",
                "record_column", "Magnetic field Z", "uT", "magnetic_field_z",
                "Raw magnetometer Z sample used for compass orientation.",
            },
        },
    };
}

SqlTableDefinition imuHeaderTable()
{
    return {
        "ImuHeader",
        {
            {"HeaderIndex", "INTEGER"},
            {"StartElapsedUs", "INTEGER"},
            {"Epoch", "INTEGER"},
            {"Millisecond", "INTEGER"},
            {"Temperature", "REAL"},
        },
        {},
    };
}

SqlTableDefinition imuPressureTable()
{
    return {
        "ImuPressure",
        {{"ElapsedUs", "INTEGER"}, {"Pressure", "REAL"}},
        {{
            "imu_pressure",
            nullptr,
            nullptr,
            "ImuPressure",
            "ElapsedUs",
            "Pressure",
            "scalar",
            "Pressure",
            "mbar",
            "pressure",
            "IMUTag pressure sample timestamped by elapsed collection time.",
        }},
    };
}

SqlTableDefinition imuMagTable()
{
    return {
        "ImuMag",
        {
            {"ElapsedUs", "INTEGER"},
            {"mx", "REAL"},
            {"my", "REAL"},
            {"mz", "REAL"},
        },
        {
            {
                "imu_mx", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "mx",
                "scalar", "Magnetic field X", "uT", "magnetic_field_x",
                "IMUTag magnetometer X sample timestamped by elapsed collection time.",
            },
            {
                "imu_my", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "my",
                "scalar", "Magnetic field Y", "uT", "magnetic_field_y",
                "IMUTag magnetometer Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_mz", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "mz",
                "scalar", "Magnetic field Z", "uT", "magnetic_field_z",
                "IMUTag magnetometer Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

SqlTableDefinition imuAccelTable()
{
    return {
        "ImuAccel",
        {
            {"ElapsedUs", "INTEGER"},
            {"ax", "REAL"},
            {"ay", "REAL"},
            {"az", "REAL"},
        },
        {
            {
                "imu_ax", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "ax",
                "scalar", "Acceleration X", "mg", "acceleration_x",
                "IMUTag accelerometer X sample timestamped by elapsed collection time.",
            },
            {
                "imu_ay", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "ay",
                "scalar", "Acceleration Y", "mg", "acceleration_y",
                "IMUTag accelerometer Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_az", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "az",
                "scalar", "Acceleration Z", "mg", "acceleration_z",
                "IMUTag accelerometer Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

SqlTableDefinition imuGyroTable()
{
    return {
        "ImuGyro",
        {
            {"ElapsedUs", "INTEGER"},
            {"gx", "REAL"},
            {"gy", "REAL"},
            {"gz", "REAL"},
        },
        {
            {
                "imu_gx", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gx",
                "scalar", "Gyroscope X", "dps", "angular_velocity_x",
                "IMUTag gyroscope X sample timestamped by elapsed collection time.",
            },
            {
                "imu_gy", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gy",
                "scalar", "Gyroscope Y", "dps", "angular_velocity_y",
                "IMUTag gyroscope Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_gz", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gz",
                "scalar", "Gyroscope Z", "dps", "angular_velocity_z",
                "IMUTag gyroscope Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

} // namespace

// Pimpl keeps the C sqlite3 types out of sqlitelog.h. This matters because the
// host library is used by Qt and non-Qt tools, and public headers should not
// force every consumer to include sqlite3.h.
class SqliteTagLogWriter::Impl
{
public:
    Impl(const std::string &path, const Config &config, bool replace_existing)
        : config_(config)
    {
        if (!isSupportedTag()) {
            setLastError("SQLite log output does not support tag type "
                         + TagType_Name(config_.tag_type()));
            return;
        }

        if (replace_existing) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }

        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            setLastSqliteError("Could not open database");
            close();
        }
    }

    ~Impl()
    {
        close();
    }

    bool isOpen() const
    {
        return db_ != nullptr;
    }

    const std::string &lastError() const
    {
        return last_error_;
    }

    bool writeHeader(Tag &tag)
    {
        if (!db_) {
            setLastError("Database is not open");
            return false;
        }

        if (!isSupportedTag()) {
            setLastError("SQLite log output does not support tag type "
                         + TagType_Name(config_.tag_type()));
            return false;
        }

        TagInfo info;
        Config config;
        CalibrationConstants constants;

        if (!tag.GetConfig(config)) {
            setLastError("Could not read tag config");
            return false;
        }

        if (config.tag_type() != config_.tag_type()) {
            setLastError("Tag config changed while preparing SQLite log output");
            return false;
        }

        google::protobuf::util::JsonPrintOptions options;
        options.add_whitespace = true;
        options.preserve_proto_field_names = true;

        // The info table intentionally stores flexible field/value pairs. That
        // matches the schema compviz already reads and lets us add metadata
        // later without changing the table layout.
        if (!exec("CREATE TABLE info ("
                  "fieldname TEXT,"
                  "value TEXT"
                  ");")) {
            return false;
        }
        log_debug("Created SQLite info table");

        if (!insertInfo("tagtype", TagType_Name(config.tag_type()))) {
            return false;
        }

        if (!createSchemaInfoTable() || !createStreamsTable()) {
            return false;
        }

        tag.GetTagInfo(info);
        if (!insertInfo("uuid", info.uuid())) {
            return false;
        }

        std::string info_json;
        std::string config_json;
        std::string constants_json;

        if (!MessageToJsonString(info, &info_json, options).ok()) {
            setLastError("Could not create JSON string for tag info");
            return false;
        }
        if (!insertInfo("info", info_json)) {
            return false;
        }

        if (!MessageToJsonString(config, &config_json, options).ok()) {
            setLastError("Could not create JSON string for tag config");
            return false;
        }
        if (!insertInfo("config", config_json)) {
            return false;
        }

        const SqlTagProfile profile = sqliteProfileForTag(config_.tag_type());
        if (profile.has_calibration_table) {
            // Store calibration entries separately so viewers can choose the
            // latest constants or inspect historical calibration updates.
            if (!exec("CREATE TABLE Calibration ("
                      "Epoch INTEGER,"
                      "Constants TEXT"
                      ");")) {
                return false;
            }
            log_debug("Created SQLite calibration table");

            Statement calibration_insert(
                db_,
                "INSERT INTO Calibration (Epoch, Constants) VALUES (?, ?)");
            if (!calibration_insert.valid()) {
                setLastSqliteError("Could not prepare calibration insert");
                return false;
            }

            // ReadCalibration(index) returns false when there are no more
            // entries.
            for (int i = 0; tag.ReadCalibration(constants, i); i++) {
                constants_json.clear();
                if (!MessageToJsonString(constants, &constants_json, options).ok()) {
                    setLastError("Could not create JSON string for tag calibration constants");
                    return false;
                }
                if (!calibration_insert.bindInt64(1, constants.timestamp())
                    || !calibration_insert.bindText(2, constants_json)
                    || !calibration_insert.stepDone()) {
                    setLastSqliteError("Calibration constants insert failed");
                    return false;
                }
            }
        }

        // State history is useful for reconstructing the tag lifecycle around a
        // download. Keep column names aligned with the INSERT below; older code
        // accidentally used "Calibration" here while inserting "State".
        if (!exec("CREATE TABLE states ("
                  "Epoch INTEGER,"
                  "State TEXT,"
                  "EntryCode TEXT,"
                  "Temperature REAL,"
                  "Voltage REAL,"
                  "InternalLogSize INTEGER,"
                  "ExternalLogSize INTEGER"
                  ");")) {
            return false;
        }
        log_debug("Created SQLite states table");

        Statement state_insert(
            db_,
            "INSERT INTO states (Epoch, State, EntryCode, Temperature, Voltage, "
            "InternalLogSize, ExternalLogSize) VALUES (?, ?, ?, ?, ?, ?, ?)");
        if (!state_insert.valid()) {
            setLastSqliteError("Could not prepare state insert");
            return false;
        }

        StateLog state_log;
        int next = 0;
        while (tag.GetStateLog(state_log, next)) {
            // The tag returns state history in chunks. Advance by the number of
            // states received so the next request continues where this one left.
            next += state_log.states().size();
            for (auto const &state : state_log.states()) {
                if (!state_insert.bindInt64(1, state.status().millis() / 1000)
                    || !state_insert.bindText(2, TagState_Name(state.status().state()))
                    || !state_insert.bindText(3, State_Event_Name(state.transition_reason()))
                    || !state_insert.bindDouble(4, state.status().temperature())
                    || !state_insert.bindDouble(5, state.status().voltage())
                    || !state_insert.bindInt64(6, state.status().internal_data_count())
                    || !state_insert.bindInt64(7, state.status().external_data_count())
                    || !state_insert.stepDone()) {
                    setLastSqliteError("State insertion failed");
                    return false;
                }
            }
        }

        return true;
    }

    int writeLog(const Ack &ack)
    {
        if (!db_) {
            setLastError("Database is not open");
            return -2;
        }

        switch (config_.tag_type()) {
        case BITTAG:
            if (!ack.has_bittag_data_log()) {
                return 0;
            }
            return dumpBitTagLog(ack.bittag_data_log());

        case COMPASSTAG:
            if (!ack.has_compasstag_data_log()) {
                // The tag signals end-of-log by returning an Ack without
                // another data-log payload. That is a normal termination
                // condition, not a SQLite/write error.
                return 0;
            }
            return dumpCompassTagLog(ack.compasstag_data_log());

        case PRESTAG:
            if (!ack.has_prestag_data_log()) {
                return 0;
            }
            return dumpPresTagLog(ack.prestag_data_log());

        case BITPRESTAG:
            if (!ack.has_bitprestag_data_log()) {
                return 0;
            }
            return dumpBitPresTagLog(ack.bitprestag_data_log());

        case IMUTAG:
            if (!ack.has_imu_data_log()) {
                return 0;
            }
            return dumpIMUTagLog(ack.imu_data_log());

        default:
            setLastError("SQLite log output does not support tag type "
                         + TagType_Name(config_.tag_type()));
            return -1;
        }
    }

private:
    // ---------------------------------------------------------------------
    // Basic SQLite Helpers
    // ---------------------------------------------------------------------
    //
    // These small helpers keep direct sqlite3 API handling localized. They do
    // not know about tag schemas; they only execute SQL and report errors in
    // the TagLogWriter/logging style used by the rest of tagcore.

    bool isSupportedTag() const
    {
        return isTagLogStorageFormatSupported(config_.tag_type(), TagLogStorageFormat::Sqlite);
    }

    bool exec(const char *sql)
    {
        char *error = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            last_error_ = error ? error : sqlite3_errmsg(db_);
            sqlite3_free(error);
            log_error("%s", last_error_.c_str());
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    // Generic Schema Creation
    // ---------------------------------------------------------------------
    //
    // These functions create the metadata tables and sensor data tables from
    // the schema profile. They are intentionally separate from the tag-specific
    // payload writers below: table existence and stream meaning are defined by
    // SqlTagProfile/SqlTableDefinition, while protobuf unpacking still depends
    // on each tag's log message format.

    bool createSchemaInfoTable()
    {
        if (!exec("CREATE TABLE schema_info ("
                  "key TEXT PRIMARY KEY,"
                  "value TEXT"
                  ");")) {
            return false;
        }

        return insertKeyValue("schema_info", "log_schema_version", "1")
            && insertKeyValue("schema_info", "producer", "tagcore.SqliteTagLogWriter");
    }

    bool createStreamsTable()
    {
        if (!exec("CREATE TABLE streams ("
                  "stream_id TEXT PRIMARY KEY,"
                  "group_id TEXT,"
                  "group_name TEXT,"
                  "table_name TEXT NOT NULL,"
                  "time_column TEXT NOT NULL,"
                  "value_column TEXT NOT NULL,"
                  "stream_kind TEXT NOT NULL,"
                  "display_name TEXT NOT NULL,"
                  "units TEXT,"
                  "quantity TEXT,"
                  "comment TEXT"
                  ");")) {
            return false;
        }

        Statement insert(
            db_,
            "INSERT INTO streams ("
            "stream_id, group_id, group_name, table_name, time_column, value_column, "
            "stream_kind, display_name, units, quantity, comment"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        if (!insert.valid()) {
            setLastSqliteError("Could not prepare stream metadata insert");
            return false;
        }

        const SqlTagProfile profile = sqliteProfileForTag(config_.tag_type());
        for (const SqlTableDefinition &table : profile.tables) {
            for (const SqlStreamDefinition &stream : table.streams) {
                if (!insert.bindText(1, stream.id)
                    || !bindNullableText(insert, 2, stream.group_id)
                    || !bindNullableText(insert, 3, stream.group_name)
                    || !insert.bindText(4, stream.table)
                    || !insert.bindText(5, stream.time_column)
                    || !insert.bindText(6, stream.value_column)
                    || !insert.bindText(7, stream.kind)
                    || !insert.bindText(8, stream.display_name)
                    || !bindNullableText(insert, 9, stream.units)
                    || !bindNullableText(insert, 10, stream.quantity)
                    || !bindNullableText(insert, 11, stream.comment)
                    || !insert.stepDone()) {
                    setLastSqliteError("Stream metadata insert failed");
                    return false;
                }
            }
        }

        return true;
    }

    bool createLogTables()
    {
        if (log_tables_created_) {
            return true;
        }

        const SqlTagProfile profile = sqliteProfileForTag(config_.tag_type());
        if (profile.tables.empty()) {
            setLastError("SQLite log output does not support tag type "
                         + TagType_Name(config_.tag_type()));
            return false;
        }

        for (const SqlTableDefinition &table : profile.tables) {
            if (!createTable(table)) {
                return false;
            }
        }

        log_tables_created_ = true;
        return true;
    }

    bool createTable(const SqlTableDefinition &table)
    {
        std::ostringstream sql;
        sql << "CREATE TABLE " << table.name << " (";
        for (size_t i = 0; i < table.columns.size(); i++) {
            if (i > 0) {
                sql << ",";
            }
            sql << table.columns[i].name << " " << table.columns[i].type;
        }
        sql << ");";

        return exec(sql.str().c_str());
    }

    // ---------------------------------------------------------------------
    // Generic Row Writers
    // ---------------------------------------------------------------------
    //
    // These write rows to metadata tables created above. They are generic in
    // the sense that they do not decode tag log payloads; they only persist
    // key/value fields and stream catalog rows.

    // Record the metadata table's common insert pattern in one place. SQLite
    // parameter indexes are 1-based.
    bool insertInfo(const std::string &fieldname, const std::string &value)
    {
        Statement insert(db_, "INSERT INTO info (fieldname, value) VALUES (?, ?)");
        if (!insert.valid()) {
            setLastSqliteError("Could not prepare info insert");
            return false;
        }
        if (!insert.bindText(1, fieldname)
            || !insert.bindText(2, value)
            || !insert.stepDone()) {
            setLastSqliteError("Info insert failed");
            return false;
        }
        return true;
    }

    bool insertKeyValue(
        const char *table,
        const std::string &key,
        const std::string &value)
    {
        const std::string sql =
            std::string("INSERT INTO ") + table + " (key, value) VALUES (?, ?)";
        Statement insert(db_, sql.c_str());
        if (!insert.valid()) {
            setLastSqliteError(std::string("Could not prepare ") + table + " insert");
            return false;
        }
        if (!insert.bindText(1, key)
            || !insert.bindText(2, value)
            || !insert.stepDone()) {
            setLastSqliteError(std::string(table) + " insert failed");
            return false;
        }
        return true;
    }

    bool bindNullableText(Statement &statement, int index, const char *value)
    {
        if (value) {
            return statement.bindText(index, value);
        }
        return statement.bindNull(index);
    }

    // ---------------------------------------------------------------------
    // Tag-Specific Payload Writers
    // ---------------------------------------------------------------------
    //
    // The schema catalog decides which tables exist, but each tag protobuf has
    // different timing and packing rules. These functions decode those payloads
    // and write rows into the already-created schema tables.

    int dumpBitTagLog(const BitTagLog &log)
    {
        if (!createLogTables()) {
            return -2;
        }

        int bucket_bits = 0;
        int bucket_number = 0;
        int bucket_period = 0;

        switch (config_.bittag_log()) {
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
            setLastError("BitTag SQLite output requires a configured log format");
            return -2;
        }

        Statement voltage_insert(db_, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
        Statement temperature_insert(
            db_,
            "INSERT INTO CoreTemperature (Epoch, Temperature) VALUES (?, ?)");
        Statement activity_insert(db_, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");

        if (!voltage_insert.valid()
            || !temperature_insert.valid()
            || !activity_insert.valid()) {
            setLastSqliteError("Could not prepare BitTag log insert");
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
                setLastSqliteError("BitTag log header insert failed");
                return -2;
            }

            const uint64_t rawdata = entry.rawdata();
            if (config_.bittag_log() == BITTAG_BITPERSEC) {
                for (int i = 0; i < 60; i++) {
                    const sqlite3_int64 sample_timestamp = timestamp - (59 - i);
                    const double activity = ((rawdata >> i) & 1) * 100.0;
                    if (!activity_insert.bindInt64(1, sample_timestamp)
                        || !activity_insert.bindDouble(2, activity)
                        || !activity_insert.stepDone()) {
                        setLastSqliteError("BitTag activity insert failed");
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
                        setLastSqliteError("BitTag activity insert failed");
                        return -2;
                    }
                }
            }
        }

        return log.data().size();
    }

    int dumpCompassTagLog(const CompassTagLog &log)
    {
        if (!createLogTables()) {
            return -2;
        }

        // CompassTag records contain voltage/temperature at the packet epoch,
        // followed by 15-second sample slots for activity and compass data.
        sqlite3_int64 timestamp = log.epoch();

        Statement voltage_insert(db_, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
        Statement temperature_insert(
            db_,
            "INSERT INTO CoreTemperature (Epoch, Temperature) VALUES (?, ?)");
        Statement activity_insert(db_, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");
        Statement compass_insert(
            db_,
            "INSERT INTO Compass (Epoch, ax, ay, az, mx, my, mz) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");

        if (!voltage_insert.valid()
            || !temperature_insert.valid()
            || !activity_insert.valid()
            || !compass_insert.valid()) {
            setLastSqliteError("Could not prepare log insert");
            return -2;
        }

        if (!voltage_insert.bindInt64(1, timestamp)
            || !voltage_insert.bindDouble(2, log.voltage())
            || !voltage_insert.stepDone()
            || !temperature_insert.bindInt64(1, timestamp)
            || !temperature_insert.bindDouble(2, log.temperature())
            || !temperature_insert.stepDone()) {
            setLastSqliteError("Log header insert failed");
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
                setLastSqliteError("Log data insert failed");
                return -2;
            }
        }

        return 1;
    }

    int dumpBitPresTagLog(const BitPresTagLog &log)
    {
        if (!createLogTables()) {
            return -2;
        }

        // BitPresTag text logs define each packet as one voltage header sample
        // followed by one-minute pressure/temperature records. Activity is
        // packed as five 6-bit buckets in each record and reported as percent
        // active over a 60-second interval.
        constexpr int bucket_number = 4; //5;
        constexpr int bucket_bits = 4; //6;
        constexpr int bucket_period = 15; //60;

        sqlite3_int64 timestamp = log.epoch();

        Statement voltage_insert(db_, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
        Statement activity_insert(db_, "INSERT INTO Activity (Epoch, Activity) VALUES (?, ?)");
        Statement pressure_insert(db_, "INSERT INTO Pressure (Epoch, Pressure) VALUES (?, ?)");
        Statement temperature_insert(
            db_,
            "INSERT INTO Temperature (Epoch, Temperature) VALUES (?, ?)");

        if (!voltage_insert.valid()
            || !activity_insert.valid()
            || !pressure_insert.valid()
            || !temperature_insert.valid()) {
            setLastSqliteError("Could not prepare BitPresTag log insert");
            return -2;
        }

        if (!voltage_insert.bindInt64(1, timestamp)
            || !voltage_insert.bindDouble(2, log.voltage())
            || !voltage_insert.stepDone()) {
            setLastSqliteError("BitPresTag log header insert failed");
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
                    setLastSqliteError("BitPresTag activity insert failed");
                    return -2;
                }
            }

            if (!pressure_insert.bindInt64(1, timestamp)
                || !pressure_insert.bindDouble(2, entry.pressure())
                || !pressure_insert.stepDone()
                || !temperature_insert.bindInt64(1, timestamp)
                || !temperature_insert.bindDouble(2, entry.temperature())
                || !temperature_insert.stepDone()) {
                setLastSqliteError("BitPresTag pressure/temperature insert failed");
                return -2;
            }
        }

        return 1;
    }

    int dumpPresTagLog(const PresTagLog &log)
    {
        if (!createLogTables()) {
            return -2;
        }

        if (config_.period() == 0) {
            setLastError("PresTag SQLite output requires a nonzero sample period");
            return -2;
        }

        sqlite3_int64 timestamp = log.epoch();

        Statement voltage_insert(db_, "INSERT INTO Voltage (Epoch, Voltage) VALUES (?, ?)");
        Statement pressure_insert(db_, "INSERT INTO Pressure (Epoch, Pressure) VALUES (?, ?)");
        Statement temperature_insert(
            db_,
            "INSERT INTO Temperature (Epoch, Temperature) VALUES (?, ?)");

        if (!voltage_insert.valid()
            || !pressure_insert.valid()
            || !temperature_insert.valid()) {
            setLastSqliteError("Could not prepare PresTag log insert");
            return -2;
        }

        if (!voltage_insert.bindInt64(1, timestamp)
            || !voltage_insert.bindDouble(2, log.voltage())
            || !voltage_insert.stepDone()) {
            setLastSqliteError("PresTag log header insert failed");
            return -2;
        }

        for (auto const &entry : log.data()) {
            if (!pressure_insert.bindInt64(1, timestamp)
                || !pressure_insert.bindDouble(2, entry.pressure())
                || !pressure_insert.stepDone()
                || !temperature_insert.bindInt64(1, timestamp)
                || !temperature_insert.bindDouble(2, entry.temperature())
                || !temperature_insert.stepDone()) {
                setLastSqliteError("PresTag log data insert failed");
                return -2;
            }

            timestamp += config_.period();
        }

        return 1;
    }

    int dumpIMUTagLog(const IMUTagLog &log)
    {
        if (!createLogTables()) {
            return -2;
        }

        if (!config_.has_lsm6()) {
            setLastError("IMUTag SQLite output requires an LSM6 configuration");
            return -2;
        }

        const int odr_hz = imuOdrHz(config_.lsm6().odr());
        if (odr_hz <= 0) {
            setLastError("IMUTag SQLite output requires a valid IMU ODR");
            return -2;
        }

        const sqlite3_int64 sample_period_us = 1000000 / odr_hz;
        const sqlite3_int64 block_period_us = sample_period_us * kImuSamplesPerBlock;
        const double accel_scale = imuAccelMgPerLsb(config_.lsm6().accel_rng());
        const double gyro_scale = imuGyroDpsPerLsb(config_.lsm6().gyro_rng());

        Statement header_insert(
            db_,
            "INSERT INTO ImuHeader "
            "(HeaderIndex, StartElapsedUs, Epoch, Millisecond, Temperature) "
            "VALUES (?, ?, ?, ?, ?)");
        Statement pressure_insert(
            db_,
            "INSERT INTO ImuPressure (ElapsedUs, Pressure) VALUES (?, ?)");
        Statement mag_insert(
            db_,
            "INSERT INTO ImuMag (ElapsedUs, mx, my, mz) VALUES (?, ?, ?, ?)");
        Statement accel_insert(
            db_,
            "INSERT INTO ImuAccel (ElapsedUs, ax, ay, az) VALUES (?, ?, ?, ?)");
        Statement gyro_insert(
            db_,
            "INSERT INTO ImuGyro (ElapsedUs, gx, gy, gz) VALUES (?, ?, ?, ?)");

        if (!header_insert.valid()
            || !pressure_insert.valid()
            || !mag_insert.valid()
            || !accel_insert.valid()
            || !gyro_insert.valid()) {
            setLastSqliteError("Could not prepare IMUTag log insert");
            return -2;
        }

        const sqlite3_int64 header_start_us =
            static_cast<sqlite3_int64>(imu_block_count_) * block_period_us;
        if (!header_insert.bindInt64(1, static_cast<sqlite3_int64>(imu_header_count_))
            || !header_insert.bindInt64(2, header_start_us)
            || !header_insert.bindInt64(3, log.epoch())
            || !header_insert.bindInt64(4, log.millisecond())
            || !header_insert.bindDouble(5, log.termperature())
            || !header_insert.stepDone()) {
            setLastSqliteError("IMUTag header insert failed");
            return -2;
        }
        imu_header_count_++;

        for (auto const &entry : log.data()) {
            const std::string &payload = entry.data();
            const uint8_t *raw = reinterpret_cast<const uint8_t *>(payload.data());
            size_t raw_size = payload.size();
            if (raw_size == kImuDataLogBytes) {
                raw += kImuRawDataOffset;
                raw_size -= kImuRawDataOffset;
            }
            if (raw_size != kImuRawDataBytes) {
                std::ostringstream error;
                error << "IMUTag data block has " << payload.size()
                      << " bytes, expected " << kImuRawDataBytes
                      << " raw bytes or " << kImuDataLogBytes
                      << " t_DataLog bytes";
                setLastError(error.str());
                return -2;
            }

            const sqlite3_int64 block_start_us =
                static_cast<sqlite3_int64>(imu_block_count_) * block_period_us;

            if (!pressure_insert.bindInt64(1, block_start_us)
                || !pressure_insert.bindDouble(2, entry.pressure())
                || !pressure_insert.stepDone()
                || !mag_insert.bindInt64(1, block_start_us)
                || !mag_insert.bindDouble(2, entry.mx())
                || !mag_insert.bindDouble(3, entry.my())
                || !mag_insert.bindDouble(4, entry.mz())
                || !mag_insert.stepDone()) {
                setLastSqliteError("IMUTag pressure/magnetometer insert failed");
                return -2;
            }

            for (int sample = 0; sample < kImuSamplesPerBlock; sample++) {
                const uint8_t *p = raw + sample * kImuBytesPerSample;
                const int16_t gx = readLeI16(p);
                const int16_t gy = readLeI16(p + 2);
                const int16_t gz = readLeI16(p + 4);
                const int16_t ax = readLeI16(p + 6);
                const int16_t ay = readLeI16(p + 8);
                const int16_t az = readLeI16(p + 10);
                const sqlite3_int64 sample_elapsed_us =
                    block_start_us + sample * sample_period_us;

                if (!accel_insert.bindInt64(1, sample_elapsed_us)
                    || !accel_insert.bindDouble(2, ax * accel_scale)
                    || !accel_insert.bindDouble(3, ay * accel_scale)
                    || !accel_insert.bindDouble(4, az * accel_scale)
                    || !accel_insert.stepDone()
                    || !gyro_insert.bindInt64(1, sample_elapsed_us)
                    || !gyro_insert.bindDouble(2, gx * gyro_scale)
                    || !gyro_insert.bindDouble(3, gy * gyro_scale)
                    || !gyro_insert.bindDouble(4, gz * gyro_scale)
                    || !gyro_insert.stepDone()) {
                    setLastSqliteError("IMUTag IMU sample insert failed");
                    return -2;
                }
            }

            imu_block_count_++;
        }

        return 1;
    }

    void setLastError(const std::string &error)
    {
        last_error_ = error;
        // lastError() is for direct callers; log_error() is what lets Qt host
        // applications and CLI tools observe the same failure through their
        // configured logging sinks.
        log_error("%s", last_error_.c_str());
    }

    void setLastSqliteError(const std::string &context)
    {
        last_error_ = context;
        if (db_) {
            last_error_ += ": ";
            last_error_ += sqlite3_errmsg(db_);
        }
        log_error("%s", last_error_.c_str());
    }

    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    Config config_;
    bool log_tables_created_ = false;
    uint64_t imu_block_count_ = 0;
    uint64_t imu_header_count_ = 0;
    std::string last_error_;
};

SqliteTagLogWriter::SqliteTagLogWriter(
    const std::string &path,
    const Config &config,
    bool replace_existing)
    : impl_(std::make_unique<Impl>(path, config, replace_existing))
{
}

SqliteTagLogWriter::~SqliteTagLogWriter() = default;

bool SqliteTagLogWriter::isOpen() const
{
    return impl_->isOpen();
}

const std::string &SqliteTagLogWriter::lastError() const
{
    return impl_->lastError();
}

bool SqliteTagLogWriter::writeHeader(Tag &tag)
{
    return impl_->writeHeader(tag);
}

int SqliteTagLogWriter::writeLog(const Ack &ack)
{
    return impl_->writeLog(ack);
}
