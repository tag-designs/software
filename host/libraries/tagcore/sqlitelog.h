#ifndef SQLITE_LOG_H
#define SQLITE_LOG_H

#include <memory>
#include <string>

#include "taglogwriter.h"
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
 * The current data-log writer supports BitTag, BitPresTag, CompassTag, and
 * PresTag records. New databases include schema_info and streams metadata
 * tables so external tools can discover stream ids, labels, units, and source
 * columns without depending on sensorViz source code. writeLog() preserves the
 * shared download convention: positive values mean records were consumed, 0
 * means no matching log payload in the Ack, and negative values mean an error
 * or unsupported tag.
 */
class SqliteTagLogWriter : public TagLogWriter
{
public:
    /**
     * Opens path as a SQLite database for the tag type described by config.
     *
     * When replace_existing is true, any existing file at path is removed
     * before opening. Removal errors are ignored so sqlite3_open() remains the
     * final authority on whether the path can be created or reused.
     */
    SqliteTagLogWriter(
        const std::string &path,
        const Config &config,
        bool replace_existing = true);
    ~SqliteTagLogWriter();

    SqliteTagLogWriter(const SqliteTagLogWriter &) = delete;
    SqliteTagLogWriter &operator=(const SqliteTagLogWriter &) = delete;

    bool isOpen() const override;
    const std::string &lastError() const override;

    /**
     * Creates metadata tables and writes tag info, config, calibration history,
     * and state history. Must be called once before writeLog().
     */
    bool writeHeader(Tag &tag) override;

    /**
     * Appends one Ack of tag data to the SQLite log tables.
     *
     * Returns the number of download records consumed using the shared
     * TagLogWriter convention.
     */
    int writeLog(const Ack &ack) override;

private:
    // Hide sqlite3.h and statement-management details from public consumers.
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
