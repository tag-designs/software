/**
 * @file    statecapture.h
 * @brief   Capture a tag's registers, internal flash and SRAM over SWD.
 *
 * @details The first thing done to a returned tag. Reads, in order of how much
 *          they disturb the tag:
 *
 *          1. registers: FLASH_OPTR, the flash ECC registers (before the flash
 *             is read, since reading can overwrite them), RCC_CSR reset flags,
 *             RCC_BDCR, unique ID, flash size, RTC time and date, the backup
 *             registers, and raw blocks of the RCC, FLASH, PWR/TAMP, RTC and
 *             DBGMCU registers; then OTP and memory-mapped option bytes;
 *          2. the whole internal flash, page by page, then the ECC registers
 *             again;
 *          3. SRAM, only when asked for. Tags spend their idle time in
 *             Shutdown or Standby, which do not keep SRAM, so it rarely holds
 *             anything a returned tag can tell us; the state that matters is
 *             in the backup registers, which step 1 captures.
 *
 *          Nothing on the tag runs: the session holds the core at its reset
 *          vector throughout. External flash is captured afterwards through
 *          a loader; see recovery/externalcapture.h and
 *          host/libraries/tagcore/design/swd-recovery.md.
 *
 *          The result is one timestamped directory holding one file per
 *          region and a manifest.json. A region that fails is recorded and the
 *          capture continues.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md
 */

#ifndef TAGCORE_RECOVERY_STATECAPTURE_H
#define TAGCORE_RECOVERY_STATECAPTURE_H

#include "recovery/externalcapture.h"
#include "recovery/identityrecord.h"
#include "recovery/swdsession.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tagcore::recovery {

/**
 * @struct  CaptureOptions
 * @brief   What to capture and where.
 */
struct CaptureOptions {
  std::string parent_dir = "captures"; ///< Directory that receives capture-YYYYmmdd-HHMMSS/ (UTC).
  std::string reason;                  ///< Free text recorded in the manifest.
  bool include_sram = false;           ///< Also capture SRAM1 and SRAM2 (opt-in).
  /// Also capture the external flash, through the loader the identity record
  /// names (or loader_path). Last, because the loader overwrites SRAM1.
  bool include_external = true;
  std::string loader_path;             ///< Loader image to use; empty: by name from the record.
  std::vector<std::string> loader_dirs; ///< Directories searched for NAME.stldr, NAME from the record.
  bool external_full = false;          ///< Paged parts: read every block, blank or not.
  SwdExit exit = SwdExit::HardwareReset; ///< How to leave the tag.
  std::function<void(const std::string &)> progress; ///< Optional progress lines.
};

/**
 * @struct  CapturedRegion
 * @brief   One region as captured.
 */
struct CapturedRegion {
  std::string name;                  ///< e.g. "internal_flash".
  std::string file;                  ///< File name within the capture directory.
  uint32_t addr = 0;                 ///< First byte.
  uint32_t size = 0;                 ///< Bytes.
  bool ok = false;                   ///< Every byte read.
  std::string sha256;                ///< Of the file as written.
  std::vector<AddressRange> failed;  ///< Ranges left zero.
  double seconds = 0.0;              ///< Read time.
};

/**
 * @struct  NamedRegister
 * @brief   A single register value recorded in the manifest.
 */
struct NamedRegister {
  std::string name; ///< e.g. "FLASH_OPTR".
  uint32_t addr = 0;  ///< Register address.
  uint32_t value = 0; ///< Value read; 0 when !ok.
  bool ok = false;    ///< Read succeeded.
};

/**
 * @struct  CaptureResult
 * @brief   Outcome of a capture.
 */
struct CaptureResult {
  std::string dir;                      ///< Capture directory.
  std::string manifest;                 ///< Path of manifest.json.
  std::string mcu;                      ///< Part name.
  std::string uid;                      ///< 96-bit unique ID, hex.
  SwdAttachInfo attach;                 ///< What the session observed.
  std::vector<NamedRegister> registers; ///< Named register values.
  std::vector<CapturedRegion> regions;  ///< Files written.
  IdentityRecord identity;              ///< The tag identity record, parsed from internal flash.
  bool external_attempted = false;      ///< External flash capture was tried.
  std::string external_note;            ///< Why it was skipped or how it failed.
  ExternalCaptureResult external;       ///< What it read.
  bool complete = false;                ///< Every region read in full.
  std::string error;                    ///< Why the capture stopped, if it did.
};

/**
 * @brief   Capture through an already open session.
 *
 * @param[in]  session  Open session; left open.
 * @param[in]  options  What to capture.
 * @param[out] result   Filled in as far as the capture got.
 * @return  true when the directory and manifest were written, even if some
 *          regions failed (see CaptureResult::complete).
 */
bool CaptureState(SwdSession &session, const CaptureOptions &options,
                  CaptureResult &result);

/**
 * @brief   Open a session, capture, and close it.
 *
 * @param[in]  options  What to capture, and how to leave the tag.
 * @param[out] result   Filled in as far as the capture got.
 * @param[in]  usbdev   Base to use; default selects the first found.
 * @return  as CaptureState(); false also when the session could not open.
 */
bool CaptureTag(const CaptureOptions &options, CaptureResult &result,
                UsbDev usbdev = UsbDev());

} // namespace tagcore::recovery

#endif
