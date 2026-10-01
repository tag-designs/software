/**
 * @file    externalflash.h
 * @brief   A tag's external flash, read (and in rescue, erased and
 *          programmed) through a loader's Serve() entry point.
 *
 * @details Open() downloads the loader into SRAM1 and starts
 *          `Serve(buffer, size)`, which initialises the part once and then
 *          runs commands from the `loaderService` block (include/loader_service.h).
 *          The host submits a command by writing its fields and incrementing
 *          `seq`, and waits for `ack` over SWD while the core runs. Close()
 *          sends EXIT and waits for Serve() to return into the trap.
 *
 *          Compared with the per-call ST convention (SramCall::Call() on
 *          `Init` and `Read`), this initialises once, costs no register setup
 *          per command, and reports the part's JEDEC ID, its status register
 *          as found, and where a command failed.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md, "Loader protocol"
 */

#ifndef TAGCORE_RECOVERY_EXTERNALFLASH_H
#define TAGCORE_RECOVERY_EXTERNALFLASH_H

#include "recovery/sramcall.h"
#include "recovery/swdsession.h"
#include "recovery/targetimage.h"

#include <cstdint>
#include <functional>
#include <string>

namespace tagcore::recovery {

/**
 * @class   ExternalFlash
 * @brief   A Serve() session on an open SwdSession.
 *
 * @details Open() must succeed before any other call. The SwdSession must stay
 *          open for the object's lifetime. Not thread-safe. The destructor
 *          sends EXIT if Close() was not called.
 *
 * @warning Open() overwrites the start of SRAM1.
 */
class ExternalFlash {
public:
  /** @brief Called with bytes done and total during long operations. */
  using Progress = std::function<void(uint32_t done, uint32_t total)>;

  /** @param[in] session  An open session; must outlive this object. */
  explicit ExternalFlash(SwdSession &session) : s_(session), call_(session) {}
  ~ExternalFlash();
  ExternalFlash(const ExternalFlash &) = delete;
  ExternalFlash &operator=(const ExternalFlash &) = delete;

  /**
   * @brief   Download @p loader, start Serve() and wait until it is ready.
   *
   * @param[in]  loader  A loader image with `Serve` and `loaderService`.
   * @param[out] error   Why it failed; may be nullptr.
   * @return  true when the block reports ready with a known version. On
   *          false after Serve() started, the core is halted.
   */
  bool Open(const TargetImage &loader, std::string *error = nullptr);

  /** @brief JEDEC ID read at Open(), manufacturer in bits 23:16. */
  uint32_t Jedec() const { return jedec_; }
  /** @brief Status register 1 as found at Open(), before any command. */
  uint32_t Sr1() const { return sr1_; }
  /** @brief Part size in bytes. */
  uint32_t Size() const { return size_; }
  /** @brief Erase sector size in bytes. */
  uint32_t SectorSize() const { return sector_; }
  /** @brief True when the loader image can erase and program. */
  bool Writable() const { return writable_; }

  /**
   * @brief   Read @p len bytes at flash offset @p offset.
   *
   * @param[in]  offset    Flash byte offset.
   * @param[out] out       Destination, @p len bytes.
   * @param[in]  len       Bytes to read.
   * @param[in]  progress  Optional progress callback.
   * @param[out] error     Why it failed; may be nullptr.
   * @return  true when every byte was read.
   */
  bool Read(uint32_t offset, uint8_t *out, uint32_t len,
            const Progress &progress = nullptr, std::string *error = nullptr);

  /**
   * @brief   Erase the sector holding @p offset and prove it blank.
   * @return  false on a read-only loader, a range error, or a failed erase.
   */
  bool EraseSector(uint32_t offset, std::string *error = nullptr);

  /**
   * @brief   Program @p len bytes at @p offset, verified by read-back.
   * @return  false on a read-only loader, a range error, or a failed program.
   */
  bool Program(uint32_t offset, const uint8_t *data, uint32_t len,
               std::string *error = nullptr);

  /**
   * @brief   Ask Serve() to return, and wait for it.
   * @return  true when Serve() returned into the trap.
   */
  bool Close(std::string *error = nullptr);

private:
  bool Command(uint32_t cmd, uint32_t offset, uint32_t length, int timeout_ms,
               std::string *error);
  bool Field(uint32_t off, uint32_t &value);

  SwdSession &s_;
  SramCall call_;
  bool open_ = false;
  uint32_t block_ = 0;
  uint32_t seq_ = 0;
  uint32_t jedec_ = 0, sr1_ = 0, size_ = 0, sector_ = 0;
  bool writable_ = false;
};

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_EXTERNALFLASH_H
