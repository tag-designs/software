#ifndef TXT_LOGS_H
#define TXT_LOGS_H

#include <fstream>
#include <memory>
#include <string>

#include "tag.pb.h"
#include "taglogwriter.h"

class TextTagLogWriter : public TagLogWriter
{
public:
    /**
     * Writes tag downloads to the legacy text log format.
     *
     * TextTagLogWriter owns or borrows an output stream, writes the same
     * metadata header as the old text download path, and appends decoded log
     * records using the shared TagLogWriter return convention.
     */
    explicit TextTagLogWriter(std::ostream &out);
    explicit TextTagLogWriter(const std::string &path);
    ~TextTagLogWriter() override;

    TextTagLogWriter(const TextTagLogWriter &) = delete;
    TextTagLogWriter &operator=(const TextTagLogWriter &) = delete;

    bool isOpen() const override;
    const std::string &lastError() const override;
    bool writeHeader(Tag &tag) override;
    int writeLog(const Ack &ack, const Config &config) override;

private:
    // These are class methods so all public writing goes through the stream
    // owned/borrowed by this writer. File-local helpers in txtlogs.cc format
    // individual protobuf payloads.
    bool dumpHeader(Tag &tag);
    int dumpLog(const Ack &ack, const Config &config);

    std::unique_ptr<std::ofstream> owned_stream_;
    std::ostream *out_ = nullptr;
    std::string last_error_;
};

#endif /* TXT_LOGS_H */
