#include "sqlitelog.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <sqlite3.h>
#include <string>

#include <google/protobuf/util/json_util.h>
#include <tagclass.h>

#include "sqlitelog/internal.h"

extern "C"
{
#include "log.h"
}

using namespace tagcore::sqlite_log;

/*
 * SQLite log writer architecture
 *
 * This file is the public SqliteTagLogWriter wrapper and the only file that
 * talks to Tag objects. It creates the generic metadata tables, reads tag
 * config/info/calibration/state history, and dispatches each data-log ACK to a
 * tag-specific decoder in the sqlitelog subdirectory.
 *
 * The per-tag files are intentionally narrow: they receive an already-open
 * sqlite3 handle, create their data tables on first use, and unpack one
 * protobuf log payload into rows. If adding a new tag log type, update:
 *
 *   1. schema.cc, to declare tables and stream metadata;
 *   2. internal.h, to declare the tag decoder;
 *   3. a tag-specific sqlitelog/<tag>.cc decoder;
 *   4. writeLog() below, to dispatch the ACK payload.
 */

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

        /*
         * writeHeader() builds metadata once. writeLog() is called repeatedly
         * as the monitor downloads pages. A missing tag-specific payload means
         * "no more log data" for the current tag rather than failure, so those
         * cases return 0. Negative returns are reserved for writer errors.
         */
        WriterContext context = writerContext();
        switch (config_.tag_type()) {
        case BITTAG:
            if (!ack.has_bittag_data_log()) {
                return 0;
            }
            return dumpBitTagLog(context, ack.bittag_data_log());

        case COMPASSTAG:
            if (!ack.has_compasstag_data_log()) {
                // The tag signals end-of-log by returning an Ack without
                // another data-log payload. That is a normal termination
                // condition, not a SQLite/write error.
                return 0;
            }
            return dumpCompassTagLog(context, ack.compasstag_data_log());

        case PRESTAG:
            if (ack.has_prestag_raw_data_log()) {
                return dumpPresTagRawLog(context, ack.prestag_raw_data_log());
            }
            if (ack.has_prestag_data_log()) {
                return dumpPresTagLog(context, ack.prestag_data_log());
            }
            return 0;

        case BITPRESTAG:
            if (!ack.has_bitprestag_data_log()) {
                return 0;
            }
            return dumpBitPresTagLog(context, ack.bitprestag_data_log());

        case IMUTAG:
            if (ack.has_imu_raw_data_log()) {
                return dumpIMUTagRawLog(context, ack.imu_raw_data_log());
            }
            if (ack.has_imu_data_log()) {
                return dumpIMUTagLog(context, ack.imu_data_log());
            }
            return 0;

        default:
            setLastError("SQLite log output does not support tag type "
                         + TagType_Name(config_.tag_type()));
            return -1;
        }
    }

    bool beginLog()
    {
        if (!db_) {
            setLastError("Database is not open");
            return false;
        }
        if (transaction_active_) {
            return true;
        }
        if (!exec("BEGIN IMMEDIATE TRANSACTION")) {
            setLastSqliteError("Could not begin log transaction");
            return false;
        }
        transaction_active_ = true;
        return true;
    }

    bool endLog()
    {
        if (!transaction_active_) {
            return true;
        }
        if (!exec("COMMIT")) {
            setLastSqliteError("Could not commit log transaction");
            return false;
        }
        transaction_active_ = false;
        return true;
    }

private:
    WriterContext writerContext()
    {
        return {
            db_,
            config_,
            [this]() { return createLogTables(); },
            [this](const std::string &error) { setLastError(error); },
            [this](const std::string &context) { setLastSqliteError(context); },
            &imu_decode_state_,
        };
    }

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
            if (transaction_active_) {
                (void)sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                transaction_active_ = false;
            }
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    Config config_;
    bool log_tables_created_ = false;
    bool transaction_active_ = false;
    ImuDecodeState imu_decode_state_;
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

bool SqliteTagLogWriter::beginLog()
{
    return impl_->beginLog();
}

bool SqliteTagLogWriter::endLog()
{
    return impl_->endLog();
}

int SqliteTagLogWriter::writeLog(const Ack &ack)
{
    return impl_->writeLog(ack);
}
