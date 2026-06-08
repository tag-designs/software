#ifndef TAGCORE_SQLITELOG_INTERNAL_H
#define TAGCORE_SQLITELOG_INTERNAL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <tag.pb.h>

namespace tagcore::sqlite_log {

// Private helpers shared by the SQLite writer implementation files. This
// namespace is deliberately not part of the public tagcore API.
//
// File ownership:
// - sqlitelog.cc owns the writer lifecycle, generic metadata, and dispatch.
// - schema.cc owns declarative table/stream definitions for each tag type.
// - bittag.cc, compasstag.cc, pressure.cc, and imutag.cc decode protobuf
//   payloads into rows for their tag families.
//
// Keep these helpers small and data-oriented. They are shared implementation
// details, not a second public API surface.

// Small RAII wrapper for prepared inserts. A successful step resets the
// statement so callers can bind the next row without repeating boilerplate.
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

// Viewer-facing metadata for one plottable value. The table/time/value fields
// point at the SQL table created from SqlTableDefinition. group_id/group_name
// let related record columns, such as ax/ay/az, be displayed together.
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

// Complete schema description for one tag type. The profile drives table
// creation and stream metadata only; the matching dump*Log() function is still
// responsible for inserting rows whose columns match these definitions.
struct SqlTagProfile
{
    bool has_calibration_table;
    std::vector<SqlTableDefinition> tables;
};

// IMUTag logs send one absolute timestamp per header, followed by blocks whose
// sensor samples are decoded in elapsed microseconds from the start of capture.
// This state persists across ACK pages so each downloaded header/block sequence
// keeps a monotonic elapsed timeline in the SQLite database.
struct ImuDecodeState
{
    uint64_t block_count = 0;
    uint64_t segment_block_count = 0;
    uint64_t header_count = 0;
    sqlite3_int64 collection_anchor_epoch_ms = 0;
    sqlite3_int64 elapsed_base_us = 0;
    bool have_collection_anchor = false;
};

// Shared bridge from SqliteTagLogWriter::Impl to the tag-specific decoders.
// Keeping this narrow lets the per-tag files write rows without depending on
// the writer class layout. Decoders should report all failures through these
// callbacks so SqliteTagLogWriter::lastError() remains useful to CLI and Qt
// callers.
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

int dumpBitTagLog(WriterContext &ctx, const BitTagLog &log);
int dumpCompassTagLog(WriterContext &ctx, const CompassTagLog &log);
int dumpBitPresTagLog(WriterContext &ctx, const BitPresTagLog &log);
int dumpIMUTagLog(WriterContext &ctx, const IMUTagLog &log);
int dumpPresTagLog(WriterContext &ctx, const PresTagLog &log);

} // namespace tagcore::sqlite_log

#endif
