/**
 * @file    rebuild_c.cc
 * @brief   Implements the rbd_* C interface over
 *          tagcore::recovery::RebuildSqliteFromCapture().
 *
 * @details A thin shim: it converts the C strings, calls the C++ rebuild,
 *          and copies its error message into the caller's buffer. No C++
 *          exception crosses the C boundary.
 */

#include "rebuild_c.h"

#include <cstring>
#include <exception>
#include <string>

#include "recovery/capturesource.h"

namespace {

/**
 * @brief   Copies @p message into the caller's error buffer, truncated and
 *          always NUL-terminated.
 *
 * @param[out] error      The caller's buffer; nothing is written when null.
 * @param[in]  error_len  Its size in bytes; nothing is written when 0.
 * @param[in]  message    The text to copy.
 */
void set_error(char *error, size_t error_len, const std::string &message)
{
    if (error == nullptr || error_len == 0) {
        return;
    }
    const size_t n = message.size() < error_len - 1 ? message.size() : error_len - 1;
    std::memcpy(error, message.data(), n);
    error[n] = '\0';
}

} // namespace

unsigned rbd_abi_version(void)
{
    return RBD_ABI_VERSION;
}

int rbd_rebuild(const char *capture_dir, const char *db_path, uint32_t *records,
                char *error, size_t error_len)
{
    if (records != nullptr) {
        *records = 0;
    }
    if (capture_dir == nullptr || db_path == nullptr) {
        set_error(error, error_len, "rbd_rebuild: null path");
        return 0;
    }
    try {
        std::string message;
        const bool ok =
            tagcore::recovery::RebuildSqliteFromCapture(capture_dir, db_path, records, &message);
        set_error(error, error_len, ok ? std::string() : message);
        return ok ? 1 : 0;
    } catch (const std::exception &e) {
        set_error(error, error_len, std::string("rbd_rebuild: ") + e.what());
    } catch (...) {
        set_error(error, error_len, "rbd_rebuild: unknown exception");
    }
    return 0;
}
