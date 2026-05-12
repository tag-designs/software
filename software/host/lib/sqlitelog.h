#ifndef SQLITE_LOG_H
#define SQLITE_LOG_H

#include <memory>
#include <string>

#include "tag.pb.h"

class Tag;

/**
 * Writes tag downloads to the project SQLite database format without using Qt.
 *
 * This class is intended for both Qt applications and command line tools. It
 * keeps Qt types out of the host library so callers only need the shared tag
 * library and the platform SQLite library.
 *
 * Errors are reported in two ways:
 * - lastError() holds the most recent human-readable error for callers that
 *   need to display or react to it.
 * - the implementation also emits through log_error()/log_debug(), which lets
 *   Qt host applications show messages through their existing log window hook.
 *
 * The current data-log writer supports CompassTag records because that is the
 * schema consumed by compviz today. dumpLog() preserves the existing download
 * convention: positive values mean records were consumed, 0 means no matching
 * log payload in the Ack, and negative values mean an error or unsupported tag.
 */
class SqliteTagLogWriter
{
public:
    /**
     * Opens path as a SQLite database.
     *
     * When replace_existing is true, any existing file at path is removed
     * before opening. Removal errors are ignored so sqlite3_open() remains the
     * final authority on whether the path can be created or reused.
     */
    explicit SqliteTagLogWriter(const std::string &path, bool replace_existing = true);
    ~SqliteTagLogWriter();

    SqliteTagLogWriter(const SqliteTagLogWriter &) = delete;
    SqliteTagLogWriter &operator=(const SqliteTagLogWriter &) = delete;

    bool isOpen() const;
    const std::string &lastError() const;

    /**
     * Creates metadata tables and writes tag info, config, calibration history,
     * and state history. Must be called once before dumpLog().
     */
    bool dumpHeader(Tag &tag);

    /**
     * Appends one Ack of tag data to the SQLite log tables.
     *
     * Returns the number of download records consumed using the same convention
     * as dumpTagLog() in taglogs.cc.
     */
    int dumpLog(const Ack &ack, const Config &config);

private:
    // Hide sqlite3.h and statement-management details from public consumers.
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
