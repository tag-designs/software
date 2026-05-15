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
    TextTagLogWriter(std::ostream &out, const Config &config);
    TextTagLogWriter(const std::string &path, const Config &config);
    ~TextTagLogWriter() override;

    TextTagLogWriter(const TextTagLogWriter &) = delete;
    TextTagLogWriter &operator=(const TextTagLogWriter &) = delete;

    bool isOpen() const override;
    const std::string &lastError() const override;
    bool writeHeader(Tag &tag) override;
    int writeLog(const Ack &ack) override;

private:
    // These are class methods so all public writing goes through the stream
    // owned/borrowed by this writer. File-local helpers in txtlogs.cc format
    // individual protobuf payloads.
    bool writeTextHeader(Tag &tag);
    int writeTextLog(const Ack &ack);

    std::unique_ptr<std::ofstream> owned_stream_;
    std::ostream *out_ = nullptr;
    Config config_;
    std::string last_error_;
};

#endif /* TXT_LOGS_H */
