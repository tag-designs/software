#include "sqlitelog.h"

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <string>

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

} // namespace

// Pimpl keeps the C sqlite3 types out of sqlitelog.h. This matters because the
// host library is used by Qt and non-Qt tools, and public headers should not
// force every consumer to include sqlite3.h.
class SqliteTagLogWriter::Impl
{
public:
    Impl(const std::string &path, bool replace_existing)
    {
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

    bool dumpHeader(Tag &tag)
    {
        if (!db_) {
            setLastError("Database is not open");
            return false;
        }

        TagInfo info;
        Config config;
        CalibrationConstants constants;

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

        if (!tag.GetConfig(config)) {
            setLastError("Could not read tag config");
            return false;
        }

        if (!insertInfo("tagtype", TagType_Name(config.tag_type()))) {
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

        // Store calibration entries separately so viewers can choose the latest
        // constants or inspect historical calibration updates.
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

        // ReadCalibration(index) returns false when there are no more entries.
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

    int dumpLog(const Ack &ack, const Config &config)
    {
        if (!db_) {
            setLastError("Database is not open");
            return -2;
        }

        // Keep this explicit until the schema grows support for the other tag
        // log formats handled by the text logger in txtlogs.cc.
        if (config.tag_type() != COMPASSTAG) {
            setLastError("SQLite log output only supports CompassTag logs");
            return -1;
        }

        if (!ack.has_compasstag_data_log()) {
            // The tag signals end-of-log by returning an Ack without another
            // data-log payload. That is a normal termination condition, not a
            // SQLite/write error.
            return 0;
        }

        return dumpCompassTagLog(ack.compasstag_data_log());
    }

private:
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

    bool createLogTables()
    {
        if (log_tables_created_) {
            return true;
        }

        // These names and columns are the existing CompassTag database contract
        // used by compviz/sqliteload.cpp.
        if (!exec("CREATE TABLE CoreTemperature ("
                  "Epoch INTEGER,"
                  "Temperature REAL"
                  ");")
            || !exec("CREATE TABLE Voltage ("
                     "Epoch INTEGER,"
                     "Voltage REAL"
                     ");")
            || !exec("CREATE TABLE Activity ("
                     "Epoch INTEGER,"
                     "Activity REAL"
                     ");")
            || !exec("CREATE TABLE Compass ("
                     "Epoch INTEGER,"
                     "ax REAL,"
                     "ay REAL,"
                     "az REAL,"
                     "mx REAL,"
                     "my REAL,"
                     "mz REAL"
                     ");")) {
            return false;
        }

        log_tables_created_ = true;
        log_debug("Created SQLite CompassTag log tables");
        return true;
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
    bool log_tables_created_ = false;
    std::string last_error_;
};

SqliteTagLogWriter::SqliteTagLogWriter(const std::string &path, bool replace_existing)
    : impl_(std::make_unique<Impl>(path, replace_existing))
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

bool SqliteTagLogWriter::dumpHeader(Tag &tag)
{
    return writeHeader(tag);
}

int SqliteTagLogWriter::dumpLog(const Ack &ack, const Config &config)
{
    return writeLog(ack, config);
}

bool SqliteTagLogWriter::writeHeader(Tag &tag)
{
    return impl_->dumpHeader(tag);
}

int SqliteTagLogWriter::writeLog(const Ack &ack, const Config &config)
{
    return impl_->dumpLog(ack, config);
}
