#ifndef TAG_LOG_WRITER_H
#define TAG_LOG_WRITER_H

#include <memory>
#include <string>
#include <vector>

#include "tag.pb.h"

class Tag;

enum class TagLogStorageFormat
{
    Text,
    Sqlite
};

class TagLogWriter
{
public:
    virtual ~TagLogWriter() = default;

    virtual bool isOpen() const = 0;
    virtual const std::string &lastError() const = 0;
    virtual bool writeHeader(Tag &tag) = 0;
    virtual int writeLog(const Ack &ack, const Config &config) = 0;
};

TagLogStorageFormat defaultTagLogStorageFormat(TagType tag_type);
bool isTagLogStorageFormatSupported(TagType tag_type, TagLogStorageFormat format);
std::vector<TagLogStorageFormat> supportedTagLogStorageFormats(TagType tag_type);

std::string defaultTagLogExtension(TagLogStorageFormat format);
std::string tagLogFileFilter(TagLogStorageFormat format);

std::unique_ptr<TagLogWriter> createTagLogWriter(
    TagLogStorageFormat format,
    const std::string &path);

#endif /* TAG_LOG_WRITER_H */
