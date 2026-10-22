#ifndef SQLITE_LOG_H
#define SQLITE_LOG_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "taglogwriter.h"
#include "tag.pb.h"

class Tag;

/**
 * @enum    TagLogSeries
 * @brief   The value tables a decoded-sample import can write.
 *
 * @details Each names one table of the tag's SQLite profile and its value
 *          column: Activity (%), Voltage (V), CoreTemperature (°C, the
 *          processor's), Pressure (mbar) and Temperature (°C, the pressure
 *          sensor's). Units are the ones the Ack decoders write.
 */
enum class TagLogSeries
{
    Activity,        ///< Activity.Activity, percent of the bucket.
    Voltage,         ///< Voltage.Voltage, volts.
    CoreTemperature, ///< CoreTemperature.Temperature, °C.
    Pressure,        ///< Pressure.Pressure, mbar (= hPa).
    Temperature      ///< Temperature.Temperature, °C.
};

/**
 * @struct  TagLogSample
 * @brief   One already-decoded value at a Unix time.
 */
struct TagLogSample
{
    int64_t epoch; ///< Unix seconds.
    double value;  ///< In the series' unit.
};

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
 * The current data-log writer supports BitTag, BitTagNG, BitPresTag,
 * CompassTag, IMUTag, PresTag, and UIUCTag records, including PresTag raw and
 * UIUCTag packed-sample payloads.
 * New databases include
 * schema_info and streams metadata tables so external tools can discover stream
 * ids, labels, units, and source columns without depending on sensorViz source
 * code. writeLog() preserves the shared download convention: positive values
 * mean records were consumed, 0 means no matching log payload in the Ack, and
 * negative values mean an error or unsupported tag.
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
     * @brief   Write the header from one gathered beforehand.
     *
     * @details What writeHeader(Tag &) does after readTagLogHeader(), for a
     *          header that did not come from a live tag, such as one rebuilt
     *          from an SWD capture (host/libraries/tagcore/recovery).
     *
     * @param[in] header  Config, info, calibration slots and state history.
     *                    Its config must match the one this writer was
     *                    constructed with.
     * @return  false on a mismatch or a database error; lastError() says which.
     */
    bool writeHeader(const TagLogHeader &header);

    /**
     * @brief   Add one row to the info table.
     *
     * @details For metadata the tag does not supply, such as the provenance
     *          of a log rebuilt from an SWD capture.
     *
     * @pre     writeHeader() succeeded, which creates the table.
     * @param[in] fieldname  Info key.
     * @param[in] value      Info value.
     * @return  false on a database error; lastError() says which.
     */
    bool writeInfo(const std::string &fieldname, const std::string &value);
    bool beginLog() override;
    bool endLog() override;

    /**
     * Appends one Ack of tag data to the SQLite log tables.
     *
     * Returns the number of download records consumed using the shared
     * TagLogWriter convention.
     */
    int writeLog(const Ack &ack) override;

    /**
     * @brief   Append already-decoded samples to one value table.
     *
     * @details The entry point for logs that hold values rather than tag
     *          records, such as a converted legacy .txt log
     *          (host/commandline/tag-txt-import.cc): the rows go into the same
     *          tables, with the same streams metadata, as writeLog() would
     *          write, and the Ack path is not involved. Rows are inserted in
     *          the order given, so a table's rowid order is the caller's
     *          order. Call between beginLog() and endLog() for speed.
     *
     * @pre     writeHeader() succeeded.
     * @param[in] series   The table to write.
     * @param[in] samples  Values, in the order to store them.
     * @return  false when the tag type's profile has no such table, or on a
     *          database error; lastError() says which.
     */
    bool writeSamples(TagLogSeries series, const std::vector<TagLogSample> &samples);

private:
    // Hide sqlite3.h and statement-management details from public consumers.
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
