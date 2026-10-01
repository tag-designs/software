/**
 * @file    loader_service.h
 * @brief   Command block shared by an external-flash loader's Serve() entry
 *          point and the host library that drives it over SWD.
 *
 * @details Serve() initialises the loader once, publishes this block, and then
 *          loops: the host writes a command's fields, then increments @c seq;
 *          the loader runs the command, fills @c status, @c detail and
 *          @c progress, and copies @c seq into @c ack. The host polls @c ack
 *          over SWD while the core runs. This replaces the per-call ST
 *          convention, in which each operation costs a register setup, a
 *          run/halt round trip and a re-initialisation, and returns only 0
 *          or 1.
 *
 *          The block is found by its symbol, ::loaderService, in the loader
 *          ELF. Data moves through the buffer the host passes to Serve().
 *
 *          The layout is little-endian 32-bit words with no padding. Changing
 *          it requires a new ::LOADER_SERVICE_VERSION; the host refuses a
 *          version it does not know.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md, "Loader protocol"
 * @see     embedded/loaders/design/loader-runtime.md
 */

#ifndef LOADER_SERVICE_H
#define LOADER_SERVICE_H

#include <stdint.h>

/** @brief 'LSRV': written to @c magic once the loader is ready for commands. */
#define LOADER_SERVICE_MAGIC 0x5652534CU

/** @brief Layout version of ::LoaderServiceBlock. */
#define LOADER_SERVICE_VERSION 1U

/**
 * @enum    LoaderServiceCommand
 * @brief   Commands the host may submit.
 */
typedef enum {
  LOADER_CMD_PROBE = 1,        ///< Re-read identity into detail[]; no data.
  LOADER_CMD_READ = 2,         ///< Read @c length bytes at @c offset into the buffer.
  LOADER_CMD_ERASE_SECTOR = 3, ///< Erase the sector holding @c offset (write images only).
  LOADER_CMD_PROGRAM = 4,      ///< Program @c length bytes from the buffer at @c offset (write images only).
  LOADER_CMD_EXIT = 5,         ///< Return from Serve() into the host's trap.
} LoaderServiceCommand;

/**
 * @enum    LoaderServiceStatus
 * @brief   Values of LoaderServiceBlock::status.
 */
typedef enum {
  LOADER_STATUS_OK = 0,             ///< The command completed.
  LOADER_STATUS_BAD_COMMAND = -1,   ///< Unknown command.
  LOADER_STATUS_RANGE = -2,         ///< Offset or length outside the part or the buffer.
  LOADER_STATUS_IO = -3,            ///< SPI timeout or a part that did not respond.
  LOADER_STATUS_READ_ONLY = -4,     ///< Erase or program asked of a read-only image.
  LOADER_STATUS_VERIFY = -5,        ///< Erase or program did not read back.
  LOADER_STATUS_INIT = -6,          ///< Clock, board or probe failed in Serve(); no commands follow.
} LoaderServiceStatus;

/** @brief detail[] index: JEDEC ID as read at probe, manufacturer in bits 23:16. */
#define LOADER_DETAIL_JEDEC 0
/** @brief detail[] index: status register 1 as found, before any command. */
#define LOADER_DETAIL_SR1 1
/** @brief detail[] index: part size in bytes. */
#define LOADER_DETAIL_SIZE 2
/** @brief detail[] index: erase sector size in bytes. */
#define LOADER_DETAIL_SECTOR 3
/** @brief detail[] index: 1 when the image can erase and program. */
#define LOADER_DETAIL_WRITABLE 4
/** @brief detail[] index: flash offset of the failure, for a failed command. */
#define LOADER_DETAIL_FAIL_OFFSET 5

/**
 * @struct  LoaderServiceBlock
 * @brief   The command block; see the file description for the protocol.
 */
typedef struct {
  uint32_t magic;     ///< ::LOADER_SERVICE_MAGIC once ready.
  uint32_t version;   ///< ::LOADER_SERVICE_VERSION.
  uint32_t seq;       ///< Host increments to submit a command.
  uint32_t ack;       ///< Loader copies @c seq when the command completes.
  uint32_t cmd;       ///< A ::LoaderServiceCommand.
  uint32_t offset;    ///< Flash byte offset (not the programmer's 0x90000000 address).
  uint32_t length;    ///< Bytes; at most the buffer size.
  int32_t status;     ///< A ::LoaderServiceStatus for the last command.
  uint32_t detail[8]; ///< Indexed by the LOADER_DETAIL_* values.
  uint32_t progress;  ///< Bytes done in the current command.
} LoaderServiceBlock;

#endif /* LOADER_SERVICE_H */
