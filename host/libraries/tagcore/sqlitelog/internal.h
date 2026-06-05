#ifndef TAGCORE_SQLITELOG_INTERNAL_H
#define TAGCORE_SQLITELOG_INTERNAL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <tag.pb.h>

namespace tagcore::sqlite_log {

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

struct ImuDecodeState
{
    uint64_t block_count = 0;
    uint64_t header_count = 0;
};

struct WriterContext
{
    sqlite3 *db;
    const Config &config;
    std::function<bool()> createLogTables;
    std::function<void(const std::string &)> setLastError;
    std::function<void(const std::string &)> setLastSqliteError;
    ImuDecodeState *imu = nullptr;
};

SqlTagProfile sqliteProfileForTag(TagType tag_type);

int dumpIMUTagLog(WriterContext &ctx, const IMUTagLog &log);

} // namespace tagcore::sqlite_log

#endif
