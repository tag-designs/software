#include "taglogwriter.h"

#include <memory>
#include <string>
#include <vector>

#include "sqlitelog.h"
#include "taglogs.h"

TagLogStorageFormat defaultTagLogStorageFormat(TagType tag_type)
{
    if (isTagLogStorageFormatSupported(tag_type, TagLogStorageFormat::Sqlite)) {
        return TagLogStorageFormat::Sqlite;
    }
    return TagLogStorageFormat::Text;
}

bool isTagLogStorageFormatSupported(TagType tag_type, TagLogStorageFormat format)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        return tag_type == COMPASSTAG;
    case TagLogStorageFormat::Text:
        return true;
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
    const std::string &path)
{
    switch (format) {
    case TagLogStorageFormat::Sqlite:
        return std::make_unique<SqliteTagLogWriter>(path);
    case TagLogStorageFormat::Text:
    default:
        return std::make_unique<TextTagLogWriter>(path);
    }
}
