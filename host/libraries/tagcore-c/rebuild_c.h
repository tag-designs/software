/**
 * @file    rebuild_c.h
 * @brief   C interface to the offline rebuild: a SQLite download from a
 *          tag-capture directory, without the tag.
 *
 * @details Wraps tagcore::recovery::RebuildSqliteFromCapture()
 *          (host/libraries/tagcore/recovery/capturesource.h), so the Rust
 *          tag-rebuild writes exactly the database the C++ tool wrote: the
 *          same family decoders turn the capture back into the messages the
 *          firmware would have served, and the same SqliteTagLogWriter writes
 *          them. The capture directory itself is written by the Rust
 *          tag-capture (tagcore::recovery in tagcore-rs) or the C++ one; the
 *          two are identical file for file.
 *
 *          @code
 *          uint32_t records = 0;
 *          char error[512];
 *          if (!rbd_rebuild(dir, "tag.db3", &records, error, sizeof error))
 *              report(error);
 *          @endcode
 *
 *          Diagnostics follow tlw_set_debug(), which this library shares.
 *
 * @note    rbd_rebuild() is reentrant: it keeps no state between calls, and
 *          concurrent calls must name different output files.
 *
 * @see     docs/decisions/0018-offline-rebuild-capture-backed-source.md,
 *          host/libraries/tagcore-rs/design/proposals/swd-recovery-in-rust.md
 */
#ifndef REBUILD_C_H
#define REBUILD_C_H

#include <stddef.h>
#include <stdint.h>

#include "taglogwriter_c.h" /* TLW_API */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def     RBD_ABI_VERSION
 * @brief   Version of this interface; rbd_abi_version() returns the value
 *          the library was built with.
 *
 * @details Raised whenever a function's meaning or signature changes, so a
 *          caller built against one header refuses a library built from
 *          another rather than misbehaving.
 */
#define RBD_ABI_VERSION 1u

/**
 * @brief   The interface version the library was built with.
 * @return  RBD_ABI_VERSION of the library's own header. A caller compares it
 *          with the RBD_ABI_VERSION it was built against before calling
 *          anything else.
 */
TLW_API unsigned rbd_abi_version(void);

/**
 * @brief   Rebuilds a SQLite download from a tag-capture directory.
 *
 * @details Reads the capture (manifest.json, internal flash, backup
 *          registers, external flash), selects the decoder for the family its
 *          identity record names, and writes the header and then the data log,
 *          walked as tag-dwnld walks it. Info rows record the provenance:
 *          "source" = "capture", the capture directory and its capture time.
 *          The result is the tag as found, before any recovery a live attach
 *          would run.
 *
 * @param[in]  capture_dir  NUL-terminated path of a tag-capture directory.
 *                          Must not be null.
 * @param[in]  db_path      NUL-terminated path of the SQLite file to create;
 *                          an existing file is replaced. It is left untouched
 *                          when the capture is refused, and removed when a
 *                          rebuild fails part way. Must not be null.
 * @param[out] records      Data-log records written; may be null.
 * @param[out] error        Buffer for why it failed, NUL-terminated and
 *                          truncated to fit; may be null. Set to "" on
 *                          success.
 * @param[in]  error_len    Size of @p error in bytes, terminator included;
 *                          0 writes nothing.
 *
 * @return  1 when the file was written; 0 when it was not: files missing from
 *          the capture, no identity record, a family whose decoder is not
 *          implemented, a malformed capture, a SQLite failure, a null path, or
 *          an exception inside the library (never propagated).
 */
TLW_API int rbd_rebuild(const char *capture_dir, const char *db_path, uint32_t *records,
                        char *error, size_t error_len);

#ifdef __cplusplus
}
#endif

#endif /* REBUILD_C_H */
