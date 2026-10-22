#ifndef TXT_LOGS_H
#define TXT_LOGS_H

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "tag.pb.h"
#include "taglogwriter.h"

/**
 * @struct  TextLogHeader
 * @brief   Everything the text header prints, gathered beforehand.
 *
 * @details writeHeader(Tag &) fills this from monitor calls; a caller with no
 *          tag, such as tag-txt-import's round-trip check, fills it from stored
 *          fields. Both print through the same code, so the header layout has
 *          one definition.
 */
struct TextLogHeader
{
    int64_t read_time_s = 0;          ///< Host time of the read, Unix seconds.
    double error_s = 0.0;             ///< Tag clock minus host clock, seconds.
    TagInfo info;                     ///< GetTagInfo().
    Config config;                    ///< GetConfig().
    bool has_calibration = false;     ///< A magnetometer calibration was read.
    CalibrationConstants calibration; ///< ReadCalibration(-1), when has_calibration.
    TestResult test_status = TEST_UNSPECIFIED; ///< GetStatus().test_status().
    std::vector<State> states;        ///< GetStateLog(), every chunk, in order.
};

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

    /**
     * @brief   Write the header from fields gathered beforehand.
     *
     * @details Prints exactly what writeHeader(Tag &) prints for the same
     *          values. Used to render a stored header back to text.
     *
     * @param[in] header  The header's contents.
     * @return  false when the output is not open.
     */
    bool writeHeader(const TextLogHeader &header);

    int writeLog(const Ack &ack) override;

private:
    // These are class methods so all public writing goes through the stream
    // owned/borrowed by this writer. File-local helpers in txtlogs.cc format
    // individual protobuf payloads.
    bool writeTextHeader(Tag &tag);
    void writeTextHeader(const TextLogHeader &header);
    int writeTextLog(const Ack &ack);

    std::unique_ptr<std::ofstream> owned_stream_;
    std::ostream *out_ = nullptr;
    Config config_;
    std::string last_error_;
};

#endif /* TXT_LOGS_H */
