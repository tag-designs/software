#include "taglogwriter.h"

#include <memory>
#include <string>
#include <vector>

#include "sqlitelog.h"
#include "tagclass.h"
#include "txtlogs.h"

TagLogStorageFormat defaultTagLogStorageFormat(TagType tag_type)
{
    // SQLite is the only download format; the text format is retired
    // (design/proposals/retire-text-logs.md). Every real tag type has a
    // schema, so there is no fallback.
    (void)tag_type;
    return TagLogStorageFormat::Sqlite;
}

bool isTagLogStorageFormatSupported(TagType tag_type, TagLogStorageFormat format)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        // SQLite support is explicit because each tag type needs a schema and
        // dump routine.
        return tag_type == COMPASSTAG
            || tag_type == PRESTAG
            || tag_type == BITPRESTAG
            || tag_type == BITTAG
            || tag_type == BITTAG_LE
            || tag_type == BITTAGNG
            || tag_type == IMUTAG
            || tag_type == UIUCTAG;
    case TagLogStorageFormat::Text:
        // Retired for downloads: no tag type writes new text logs.
    default:
        return false;
    }
}

std::vector<TagLogStorageFormat> supportedTagLogStorageFormats(TagType tag_type)
{
    std::vector<TagLogStorageFormat> formats;
    for (TagLogStorageFormat format : {
             TagLogStorageFormat::Sqlite,
             TagLogStorageFormat::Text,
         }) {
        if (isTagLogStorageFormatSupported(tag_type, format)) {
            formats.push_back(format);
        }
    }
    return formats;
}

std::string defaultTagLogExtension(TagLogStorageFormat format)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        return ".db3";
    case TagLogStorageFormat::Text:
    default:
        return ".txt";
    }
}

std::string tagLogFileFilter(TagLogStorageFormat format)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        return "SQLite database (*.db3)";
    case TagLogStorageFormat::Text:
    default:
        return "Text log (*.txt)";
    }
}

std::unique_ptr<TagLogWriter> createTagLogWriter(
    TagLogStorageFormat format,
    const std::string &path,
    const Config &config)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        return std::make_unique<SqliteTagLogWriter>(path, config);
    case TagLogStorageFormat::Text:
    default:
        return std::make_unique<TextTagLogWriter>(path, config);
    }
}

/* Contract documented in taglogwriter.h. */
bool readTagLogHeader(Tag &tag, TagLogHeader &header, std::string *error)
{
    header = TagLogHeader();
    if (!tag.GetConfig(header.config)) {
        if (error)
            *error = "Could not read tag config";
        return false;
    }
    if (!tag.GetTagInfo(header.info)) {
        if (error)
            *error = "Could not read tag info";
        return false;
    }
    // ReadCalibration(index) returns false when there are no more entries.
    CalibrationConstants constants;
    for (uint32_t i = 0; tag.ReadCalibration(constants, i); i++)
        header.calibration.push_back(constants);
    // The tag returns state history in chunks; advance by the number received.
    StateLog state_log;
    int next = 0;
    while (tag.GetStateLog(state_log, next)) {
        next += state_log.states().size();
        for (const State &state : state_log.states())
            header.states.push_back(state);
    }
    return true;
}
