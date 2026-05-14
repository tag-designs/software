#ifndef TAG_LOGS_H
#define TAG_LOGS_H

#include <fstream>
#include <memory>
#include <ostream>
#include <string>

#include "tag.pb.h"
#include "taglogwriter.h"

enum TagLogOutput
{
    tag_log_output_txt,
    tag_log_output_json
};

// Single procedure for dumping general tag information and state.
bool dumpTagLogHeader(std::ostream &out,
                      Tag &t,
                      enum TagLogOutput format);

// Type specific procedure for different log types. Returns the number of
// records consumed, zero when the Ack has no matching payload, and a negative
// value on error.
int dumpTagLog(std::ostream &out,
               const Ack &log,
               const Config &config,
               enum TagLogOutput format);

class TextTagLogWriter : public TagLogWriter
{
public:
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
    std::unique_ptr<std::ofstream> owned_stream_;
    std::ostream *out_ = nullptr;
    std::string last_error_;
};

#endif /* TAG_LOGS_H */
