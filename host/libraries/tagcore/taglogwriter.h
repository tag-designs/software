#ifndef TAG_LOG_WRITER_H
#define TAG_LOG_WRITER_H

#include <memory>
#include <string>
#include <vector>

#include "tag.pb.h"

class Tag;

/**
 * @struct  TagLogHeader
 * @brief   Everything the SQLite writer records about the tag, gathered
 *          before writing.
 *
 * @details SqliteTagLogWriter::writeHeader(const TagLogHeader &) takes this
 *          instead of querying a Tag. The same writer then serves both:
 *          - a live download: readTagLogHeader() fills it from monitor calls;
 *          - an offline rebuild from an SWD capture: the recovery code fills
 *            it from capture bytes.
 *          The data log follows as writeLog(Ack) calls in both cases.
 */
struct TagLogHeader
{
    Config config;                                 ///< GetConfig().
    TagInfo info;                                  ///< GetTagInfo().
    std::vector<CalibrationConstants> calibration; ///< ReadCalibration(0..n-1), oldest first.
    std::vector<State> states;                     ///< GetStateLog(), every chunk, in order.
};

/**
 * @brief   Read a TagLogHeader from a live tag, with the monitor calls the
 *          SQLite writer made itself before it took a header.
 *
 * @details GetConfig and GetTagInfo, each required; then ReadCalibration(i)
 *          for i = 0, 1, ... until one fails; then GetStateLog(next) until a
 *          reply is empty.
 *
 * @param[in]  tag     An attached tag.
 * @param[out] header  The header.
 * @param[out] error   Why it failed; may be nullptr.
 * @return  false when a required read failed.
 */
bool readTagLogHeader(Tag &tag, TagLogHeader &header, std::string *error = nullptr);

/**
 * Storage formats supported by the common tag-log download path.
 *
 * Text is the legacy line-oriented format implemented by TextTagLogWriter.
 * Sqlite is the structured database format implemented by SqliteTagLogWriter.
 */
enum class TagLogStorageFormat
{
    Text,
    Sqlite
};

/**
 * Common interface for writing downloaded tag logs.
 *
 * Qt and command-line download code should use this interface instead of
 * directly depending on the text or SQLite implementations. The writer captures
 * the tag config at construction time. The caller writes the metadata/header
 * once, then feeds each Ack returned by Tag::GetDataLog().
 */
class TagLogWriter
{
public:
    virtual ~TagLogWriter() = default;

    // True when the output file/database was opened successfully.
    virtual bool isOpen() const = 0;

    // Human-readable detail for the most recent open/header/write failure.
    virtual const std::string &lastError() const = 0;

    // Write metadata that is common to the full download.
    virtual bool writeHeader(Tag &tag) = 0;

    // Optional bracket around a sequence of data-log writes.
    virtual bool beginLog() { return true; }
    virtual bool endLog() { return true; }

    /**
     * Write one downloaded data Ack.
     *
     * Return values use the shared download-loop convention:
     * - positive: number of tag log records consumed
     * - zero: no matching log payload, normally end-of-download
     * - negative: unsupported payload or write/decode error
     */
    virtual int writeLog(const Ack &ack) = 0;
};

// Preferred format for a tag type. This can be used as a UI default while
// still allowing the user to choose any supported format.
TagLogStorageFormat defaultTagLogStorageFormat(TagType tag_type);

// Format capability checks. Tags may support more than one format, so callers
// should query support rather than assuming the default is the only option.
bool isTagLogStorageFormatSupported(TagType tag_type, TagLogStorageFormat format);
std::vector<TagLogStorageFormat> supportedTagLogStorageFormats(TagType tag_type);

// UI/file helpers for the selected storage format.
std::string defaultTagLogExtension(TagLogStorageFormat format);
std::string tagLogFileFilter(TagLogStorageFormat format);

/**
 * Create the concrete writer for the selected format.
 *
 * The config is passed to the factory because some writers need tag-type and
 * format details at construction time. For example, the SQLite writer validates
 * whether the tag type has a schema before it creates or replaces a database.
 */
std::unique_ptr<TagLogWriter> createTagLogWriter(
    TagLogStorageFormat format,
    const std::string &path,
    const Config &config);

#endif /* TAG_LOG_WRITER_H */
