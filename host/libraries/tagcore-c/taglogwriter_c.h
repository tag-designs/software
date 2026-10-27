/**
 * @file    taglogwriter_c.h
 * @brief   C interface to the SQLite tag log writer, taking wire-encoded
 *          protobuf messages.
 *
 * @details Wraps SqliteTagLogWriter (host/libraries/tagcore/sqlitelog.h) so a
 *          program in another language -- the Rust tag-dwnld first -- writes
 *          exactly the database the C++ tools write, through the same code.
 *          Every message crosses as its protobuf wire encoding (prost's
 *          encode_to_vec(), C++'s SerializeToString()), so nothing on the
 *          caller's side interprets a log format and no float or byte is
 *          converted on the way.
 *
 *          The sequence mirrors the C++ writer and tag-dwnld:
 *
 *          @code
 *          tlw_writer *w = tlw_open(path, config, config_len, 1);
 *          if (!tlw_is_open(w)) { report(tlw_last_error(w)); tlw_close(w); }
 *          tlw_header_add(w, TLW_INFO, info, info_len);
 *          ... TLW_CALIBRATION oldest first, TLW_STATE in order ...
 *          tlw_write_header(w);
 *          tlw_begin_log(w);
 *          for (index = 0;;) {             // read page `index` from the tag
 *              int n = tlw_write_log(w, ack, ack_len);
 *              if (n <= 0) break;          // 0: end of log; < 0: error
 *              index += n;
 *          }
 *          tlw_end_log(w);
 *          tlw_close(w);
 *          @endcode
 *
 *          The library is built as one shared library (libtagcore_c) with
 *          the C++ writer, libprotobuf and SQLite linked inside it and only
 *          these functions exported, so a caller links nothing else. See
 *          host/libraries/tagcore/design/proposals/rust-over-cpp-libraries.md.
 *
 * @note    A writer is not thread-safe: use each handle from one thread at a
 *          time. Separate handles are independent.
 */
#ifndef TAGLOGWRITER_C_H
#define TAGLOGWRITER_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def     TLW_API
 * @brief   Marks a function exported from the shared library.
 *
 * @details The library hides every other symbol, including its private
 *          copies of libprotobuf and SQLite, so they cannot clash with a
 *          caller's own. On Windows a DLL exports only what is marked; on
 *          macOS and Linux an export list in CMakeLists.txt does the same.
 */
#if defined(_WIN32) && defined(TAGCORE_C_BUILDING)
#define TLW_API __declspec(dllexport)
#elif defined(_WIN32)
#define TLW_API __declspec(dllimport)
#else
#define TLW_API __attribute__((visibility("default")))
#endif

/**
 * @def     TLW_ABI_VERSION
 * @brief   Version of this interface; tlw_abi_version() returns the value
 *          the library was built with.
 *
 * @details Raised whenever a function's meaning or signature changes, so a
 *          caller built against one header refuses a library built from
 *          another rather than misbehaving.
 */
#define TLW_ABI_VERSION 1u

/**
 * @brief   Opaque handle to one SQLite log being written, from tlw_open()
 *          until tlw_close().
 */
typedef struct tlw_writer tlw_writer;

/**
 * @enum    tlw_header_kind
 * @brief   Which header part a message passed to tlw_header_add() is.
 *
 * @details The values match the record kinds proposed for tag-log-import's
 *          binary input, so the two never disagree.
 */
typedef enum {
    TLW_CONFIG = 1,      ///< Config. Optional: the one given to tlw_open() is used otherwise.
    TLW_INFO = 2,        ///< TagInfo. Required, once.
    TLW_CALIBRATION = 3, ///< CalibrationConstants, zero or more, oldest first.
    TLW_STATE = 4,       ///< State, zero or more, in the order the tag returned them.
} tlw_header_kind;

/**
 * @brief   The interface version the library was built with.
 * @return  TLW_ABI_VERSION as the library saw it. A caller compares it with
 *          its own header's value before any other call.
 */
TLW_API uint32_t tlw_abi_version(void);

/**
 * @brief   Sets how much the writer logs to stderr, as the C++ tools' -d
 *          option does (host/commandline/options.cc).
 *
 * @details The library carries its own copy of tagcore's log.c, which logs
 *          at DEBUG until told otherwise. Process-wide: it affects every
 *          handle.
 *
 * @param[in] enabled  Nonzero logs at DEBUG; zero logs errors only.
 */
TLW_API void tlw_set_debug(int enabled);

/**
 * @brief   Creates a SQLite log at @p path for a tag with configuration
 *          @p config.
 *
 * @details Constructs SqliteTagLogWriter(path, config, replace_existing).
 *          The tag type in @p config chooses the schema, and must match the
 *          header's.
 *
 * @param[in] path              Output file, UTF-8.
 * @param[in] config            Wire-encoded Config.
 * @param[in] config_len        Bytes at @p config.
 * @param[in] replace_existing  Nonzero to replace an existing file; zero to
 *                              fail if one exists.
 *
 * @return  A handle, which the caller must pass to tlw_close() whatever
 *          happens. Check tlw_is_open(): when the config does not parse or
 *          the database cannot be created, the handle is returned closed and
 *          tlw_last_error() says why. NULL only when memory runs out.
 */
TLW_API tlw_writer *tlw_open(const char *path, const uint8_t *config,
                             size_t config_len, int replace_existing);

/**
 * @brief   Whether the database is open for writing.
 * @param[in] w  Handle from tlw_open(); NULL reads as closed.
 * @return  1 if open, 0 if not (see tlw_last_error()).
 */
TLW_API int tlw_is_open(const tlw_writer *w);

/**
 * @brief   Adds one part of the log header, to be written by
 *          tlw_write_header().
 *
 * @param[in] w      Handle from tlw_open().
 * @param[in] kind   Which part @p msg is (tlw_header_kind).
 * @param[in] msg    The part's wire encoding.
 * @param[in] len    Bytes at @p msg.
 *
 * @return  1 if added; 0 if @p kind is unknown or @p msg does not parse as
 *          that kind's message, with tlw_last_error() set.
 */
TLW_API int tlw_header_add(tlw_writer *w, int kind, const uint8_t *msg, size_t len);

/**
 * @brief   Writes the header gathered by tlw_header_add():
 *          SqliteTagLogWriter::writeHeader(const TagLogHeader &).
 *
 * @details Creates the info, calibration and state tables and the tag's
 *          data tables. Call once, before tlw_begin_log().
 *
 * @param[in] w  Handle from tlw_open().
 * @return  1 if written; 0 on failure, with tlw_last_error() set. It fails
 *          without writing if no TLW_INFO part was added: a log without the
 *          tag's UUID cannot be told from another tag's.
 */
TLW_API int tlw_write_header(tlw_writer *w);

/**
 * @brief   Adds a row to the info table: SqliteTagLogWriter::writeInfo().
 *
 * @param[in] w      Handle from tlw_open(), after tlw_write_header().
 * @param[in] field  Row name, UTF-8.
 * @param[in] value  Row value, UTF-8.
 * @return  1 if written; 0 on failure, with tlw_last_error() set.
 */
TLW_API int tlw_write_info(tlw_writer *w, const char *field, const char *value);

/**
 * @brief   Starts the data log: SqliteTagLogWriter::beginLog(), which opens
 *          the transaction the pages are written in.
 * @param[in] w  Handle from tlw_open(), after tlw_write_header().
 * @return  1 on success; 0 on failure, with tlw_last_error() set.
 */
TLW_API int tlw_begin_log(tlw_writer *w);

/**
 * @brief   Decodes and writes one downloaded page:
 *          SqliteTagLogWriter::writeLog().
 *
 * @details The return value drives the download: request the next page at
 *          the index advanced by it, and stop at 0. How many records a page
 *          holds, and which page ends the log, depend on the tag type, so
 *          only the writer can say.
 *
 * @param[in] w    Handle from tlw_open(), after tlw_begin_log().
 * @param[in] ack  The data-log Ack's wire encoding, as the tag returned it.
 * @param[in] len  Bytes at @p ack.
 *
 * @return  The number of log records the page consumed, which is positive;
 *          0 at the end of the log; negative on failure (the Ack does not
 *          parse, or the writer rejects the page), with tlw_last_error() set.
 */
TLW_API int tlw_write_log(tlw_writer *w, const uint8_t *ack, size_t len);

/**
 * @brief   Finishes the data log: SqliteTagLogWriter::endLog(), which
 *          commits the transaction the pages were written in.
 * @param[in] w  Handle from tlw_open(), after tlw_begin_log().
 * @return  1 on success; 0 on failure, with tlw_last_error() set.
 */
TLW_API int tlw_end_log(tlw_writer *w);

/**
 * @brief   Why the last failing call on @p w failed.
 * @param[in] w  Handle from tlw_open(); NULL gives a fixed message.
 * @return  A UTF-8 string, empty if nothing has failed. Owned by the handle
 *          and valid until the next call on it or tlw_close().
 */
TLW_API const char *tlw_last_error(const tlw_writer *w);

/**
 * @brief   Closes the database and frees the handle.
 *
 * @details A log closed before tlw_end_log() keeps what was committed; the
 *          pages of an unfinished tlw_begin_log() transaction are rolled
 *          back. NULL is ignored.
 *
 * @param[in] w  Handle from tlw_open(); invalid afterwards.
 */
TLW_API void tlw_close(tlw_writer *w);

#ifdef __cplusplus
}
#endif

#endif
