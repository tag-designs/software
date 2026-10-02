/**
 * @file    externalcapture.h
 * @brief   Capture a tag's external flash through a loader into files.
 *
 * @details Shared by tag-capture (step 5 of the SWD recovery sequence) and
 *          tag-xflash. Opens a Serve() session (ExternalFlash) and then:
 *          - for a linear part (NOR, such as the AT25XE), writes the whole
 *            part to one file;
 *          - for a paged part (SPI NAND), reads page 0 of each block raw and
 *            skips the block when it is blank, which holds for a tag that
 *            writes pages in order. Every other block is read whole, raw and
 *            through on-die ECC. The policy is here, in the host; the loader
 *            only reads pages (u375-nand-loader-plan.md, decision 5).
 *
 *          Every file written is hashed, so a manifest can record it.
 *
 * @warning The loader overwrites the start of SRAM1: capture SRAM first.
 */

#ifndef TAGCORE_RECOVERY_EXTERNALCAPTURE_H
#define TAGCORE_RECOVERY_EXTERNALCAPTURE_H

#include "recovery/swdsession.h"
#include "recovery/targetimage.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tagcore::recovery {

/**
 * @struct  ExternalCaptureOptions
 * @brief   How much of the part to read, and where to report progress.
 */
struct ExternalCaptureOptions {
  std::string prefix;          ///< File-name prefix, e.g. "external_"; may be empty.
  bool full = false;           ///< Paged parts: read every block, blank or not.
  uint32_t first_block = 0;    ///< Paged parts: first block to scan.
  uint32_t block_count = 0;    ///< Paged parts: blocks to scan; 0 = to the end.
  std::function<void(const std::string &)> progress; ///< Optional; one line per update.
};

/**
 * @struct  ExternalCaptureFile
 * @brief   One file written.
 */
struct ExternalCaptureFile {
  std::string name;    ///< Role: "external_flash", "raw", "ecc", "pages".
  std::string file;    ///< File name within the directory.
  uint64_t size = 0;   ///< Bytes.
  std::string sha256;  ///< Of the file as written.
};

/**
 * @struct  ExternalCaptureResult
 * @brief   What the capture read and found.
 */
struct ExternalCaptureResult {
  bool ok = false;               ///< Everything requested was read.
  std::string error;             ///< Why not.
  std::string loader_path;       ///< Loader image used.
  std::string loader_sha256;     ///< Of the loader image file.
  uint32_t version = 0;          ///< Serve() protocol version.
  uint32_t jedec = 0;            ///< JEDEC ID as read, manufacturer in bits 23:16.
  uint32_t sr1 = 0;              ///< Status register as found (NOR: SR1; NAND: C0h).
  uint32_t found = 0;            ///< Paged parts: A0 | B0 << 8 | C0 << 16 | F0 << 24 as found.
  bool paged = false;            ///< The part was read page by page.
  uint32_t size = 0;             ///< Data bytes in the part.
  uint32_t page_bytes = 0;       ///< Paged: bytes per page including spare.
  uint32_t pages_per_block = 0;  ///< Paged: pages per erase block.
  uint32_t blocks_scanned = 0;   ///< Paged: blocks whose page 0 was read.
  uint32_t blocks_read = 0;      ///< Paged: blocks read whole.
  uint32_t blocks_blank = 0;     ///< Paged: blocks skipped as blank.
  uint32_t blocks_marked = 0;    ///< Paged: read blocks whose page 0 spare byte 0 is not FFh.
  uint32_t pages_uncorrectable = 0; ///< Paged: pages whose ECC verdict was uncorrectable.
  double seconds = 0.0;          ///< Wall time.
  std::vector<ExternalCaptureFile> files; ///< Files written.
};

/**
 * @brief   Read the external flash through @p loader into @p dir.
 *
 * @param[in]  session  An open session, halted at the reset vector.
 * @param[in]  loader   A loader image with Serve().
 * @param[in]  dir      Existing directory for the files.
 * @param[in]  options  Range and progress.
 * @param[out] result   What was read.
 * @return  result.ok.
 */
bool CaptureExternalFlash(SwdSession &session, const TargetImage &loader,
                          const std::string &dir,
                          const ExternalCaptureOptions &options,
                          ExternalCaptureResult &result);

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_EXTERNALCAPTURE_H
