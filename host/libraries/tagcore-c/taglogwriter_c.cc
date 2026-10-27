/**
 * @file    taglogwriter_c.cc
 * @brief   The C interface in taglogwriter_c.h, over SqliteTagLogWriter.
 *
 * @details Each function parses the wire-encoded message it is given and
 *          makes the matching SqliteTagLogWriter call; nothing here knows a
 *          log format. No C++ exception crosses the C boundary: every entry
 *          point catches them and reports them as a failure with the
 *          exception's text in tlw_last_error().
 */
#include "taglogwriter_c.h"

#include <exception>
#include <memory>
#include <new>
#include <string>

#include "sqlitelog.h"
#include "taglogwriter.h"

extern "C" {
#include "log.h"
}

/**
 * @brief   The handle behind tlw_writer: the writer, the header being
 *          gathered and the last error.
 *
 * @details @c writer is null only when the config did not parse; the handle
 *          still exists so tlw_last_error() can say so.
 */
struct tlw_writer {
    std::unique_ptr<SqliteTagLogWriter> writer; ///< Null if tlw_open() failed before construction.
    TagLogHeader header;                        ///< Parts from tlw_header_add(), written by tlw_write_header().
    bool has_info = false;                      ///< A TLW_INFO part was added.
    std::string error;                          ///< Returned by tlw_last_error().
};

namespace {

/**
 * @brief   Records why a call failed.
 * @param[in,out] w        The handle.
 * @param[in]     message  The reason.
 * @return  Always 0, so a failing path can return the call's result.
 */
int fail(tlw_writer *w, std::string message)
{
    w->error = std::move(message);
    return 0;
}

/**
 * @brief   Records the writer's own error after a failing writer call.
 * @param[in,out] w  The handle, whose writer has failed.
 * @return  Always 0.
 */
int failFromWriter(tlw_writer *w)
{
    return fail(w, w->writer->lastError());
}

/**
 * @brief   Runs @p body, turning any exception into a failure.
 *
 * @param[in,out] w             The handle; null makes the call fail at once.
 * @param[in]     failed_value  What to return on any failure.
 * @param[in]     body          The call's work, returning its result.
 * @return  @p body's result, or @p failed_value when @p w is null, has no
 *          writer, or @p body throws.
 */
template <typename Body>
int guarded(tlw_writer *w, int failed_value, Body body)
{
    if (w == nullptr) {
        return failed_value;
    }
    if (!w->writer) {
        // Keep the error from tlw_open(); it says why there is no writer.
        return failed_value;
    }
    try {
        return body();
    } catch (const std::exception &e) {
        fail(w, std::string("C++ exception: ") + e.what());
    } catch (...) {
        fail(w, "C++ exception");
    }
    return failed_value;
}

/**
 * @brief   Parses @p len bytes at @p data as message @p out.
 * @return  true if they parse; a null @p data parses only when @p len is 0.
 */
bool parse(google::protobuf::Message &out, const uint8_t *data, size_t len)
{
    if (data == nullptr) {
        return len == 0 && out.ParseFromString(std::string());
    }
    return out.ParseFromArray(data, static_cast<int>(len));
}

} // namespace

uint32_t tlw_abi_version(void)
{
    return TLW_ABI_VERSION;
}

void tlw_set_debug(int enabled)
{
    log_set_level(enabled != 0 ? LOG_DEBUG : LOG_ERROR);
}

tlw_writer *tlw_open(const char *path, const uint8_t *config, size_t config_len,
                     int replace_existing)
{
    tlw_writer *w = new (std::nothrow) tlw_writer;
    if (w == nullptr) {
        return nullptr;
    }
    try {
        if (path == nullptr) {
            fail(w, "No output path");
            return w;
        }
        if (!parse(w->header.config, config, config_len)) {
            fail(w, "Config does not parse");
            return w;
        }
        w->writer = std::make_unique<SqliteTagLogWriter>(path, w->header.config,
                                                         replace_existing != 0);
        if (!w->writer->isOpen()) {
            failFromWriter(w);
        }
    } catch (const std::exception &e) {
        w->writer.reset();
        fail(w, std::string("C++ exception: ") + e.what());
    } catch (...) {
        w->writer.reset();
        fail(w, "C++ exception");
    }
    return w;
}

int tlw_is_open(const tlw_writer *w)
{
    return w != nullptr && w->writer && w->writer->isOpen() ? 1 : 0;
}

int tlw_header_add(tlw_writer *w, int kind, const uint8_t *msg, size_t len)
{
    return guarded(w, 0, [&] {
        switch (kind) {
        case TLW_CONFIG:
            // Replaces the config given to tlw_open(), which stands otherwise.
            if (!parse(w->header.config, msg, len)) {
                return fail(w, "Config does not parse");
            }
            return 1;
        case TLW_INFO:
            if (!parse(w->header.info, msg, len)) {
                return fail(w, "TagInfo does not parse");
            }
            w->has_info = true;
            return 1;
        case TLW_CALIBRATION: {
            CalibrationConstants constants;
            if (!parse(constants, msg, len)) {
                return fail(w, "CalibrationConstants does not parse");
            }
            w->header.calibration.push_back(std::move(constants));
            return 1;
        }
        case TLW_STATE: {
            State state;
            if (!parse(state, msg, len)) {
                return fail(w, "State does not parse");
            }
            w->header.states.push_back(std::move(state));
            return 1;
        }
        default:
            return fail(w, "Unknown header part " + std::to_string(kind));
        }
    });
}

int tlw_write_header(tlw_writer *w)
{
    return guarded(w, 0, [&] {
        if (!w->has_info) {
            return fail(w, "The header has no TagInfo");
        }
        return w->writer->writeHeader(w->header) ? 1 : failFromWriter(w);
    });
}

int tlw_write_info(tlw_writer *w, const char *field, const char *value)
{
    return guarded(w, 0, [&] {
        if (field == nullptr || value == nullptr) {
            return fail(w, "Info row needs a field and a value");
        }
        return w->writer->writeInfo(field, value) ? 1 : failFromWriter(w);
    });
}

int tlw_begin_log(tlw_writer *w)
{
    return guarded(w, 0, [&] { return w->writer->beginLog() ? 1 : failFromWriter(w); });
}

int tlw_write_log(tlw_writer *w, const uint8_t *ack, size_t len)
{
    return guarded(w, -1, [&] {
        Ack message;
        if (!parse(message, ack, len)) {
            fail(w, "Ack does not parse");
            return -1;
        }
        const int consumed = w->writer->writeLog(message);
        if (consumed < 0) {
            failFromWriter(w);
        }
        return consumed;
    });
}

int tlw_end_log(tlw_writer *w)
{
    return guarded(w, 0, [&] { return w->writer->endLog() ? 1 : failFromWriter(w); });
}

const char *tlw_last_error(const tlw_writer *w)
{
    if (w == nullptr) {
        return "No writer";
    }
    return w->error.c_str();
}

void tlw_close(tlw_writer *w)
{
    try {
        delete w;
    } catch (...) {
        // A destructor that throws would end the process; there is nothing to
        // report it to once the handle is gone.
    }
}
