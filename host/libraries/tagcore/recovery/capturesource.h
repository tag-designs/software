/**
 * @file    capturesource.h
 * @brief   Rebuild a tag's download, as protobuf messages, from an SWD
 *          capture directory (next-release-todo D1).
 *
 * @details A capture (tag-capture) holds what the tag's firmware would have
 *          served over the monitor: the internal flash, the backup registers
 *          and the external flash. CaptureSource turns those bytes back into
 *          the messages the firmware's handlers would have sent, so the
 *          unchanged SQLite writer produces the same file a live download
 *          does:
 *          - Header(): TagInfo (infoAck), Config (the family's readConfig()),
 *            the calibration slots (read_calibration) and the state history
 *            (system_logAck), as one TagLogHeader;
 *          - DataLog(index): the data-log Ack for one index, as the family's
 *            data_logAck() would build it, holes included.
 *
 *          Layout comes from the tag identity record in the captured image:
 *          its region table (state log, stored config, checkpoints,
 *          calibration), its BackupState map, and its strings and numbers.
 *          The data-log format is per family; the record's "decoder" string
 *          selects it. Families not yet implemented are refused, not guessed.
 *
 *          The rebuild is "as captured": it reflects the tag as found. A live
 *          download can legitimately differ: tag-dwnld --stop adds a stop
 *          marker, and the attach lets reset recovery run first.
 *
 * @see     design/offline-log-reconstruction.md, "Decisions and plan" item 1
 */

#ifndef TAGCORE_RECOVERY_CAPTURESOURCE_H
#define TAGCORE_RECOVERY_CAPTURESOURCE_H

#include "recovery/identityrecord.h"
#include "taglogwriter.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "tag.pb.h"

namespace tagcore::recovery {

class CaptureDecoder;

/**
 * @class   CaptureSource
 * @brief   The messages of a download, rebuilt from one capture directory.
 *
 * @details Open() must succeed first. Not thread-safe.
 */
class CaptureSource {
public:
  CaptureSource();
  ~CaptureSource();
  CaptureSource(const CaptureSource &) = delete;
  CaptureSource &operator=(const CaptureSource &) = delete;

  /**
   * @brief   Load a capture directory and select the family decoder.
   *
   * @param[in]  dir    A tag-capture directory (manifest.json,
   *                    internal_flash.bin, backup_regs.bin, external_*).
   * @param[out] error  Why it failed; may be nullptr.
   * @return  false when files are missing, the identity record is absent or
   *          malformed, or its decoder is not implemented.
   */
  bool Open(const std::string &dir, std::string *error = nullptr);

  /** @brief The parsed identity record. */
  const IdentityRecord &Identity() const { return identity_; }

  /**
   * @brief   The header messages: config, info, calibration, states.
   * @return  false on a malformed capture; @p error says why.
   */
  bool Header(TagLogHeader &header, std::string *error = nullptr) const;

  /**
   * @brief   The number of data-log indices the download loop walks: the
   *          Status count the firmware reports (for IMUTag, external_blocks).
   */
  uint32_t DataLogCount() const;

  /**
   * @brief   The data-log Ack for @p index, as data_logAck() would build it.
   *
   * @param[in]  index  Data-log index.
   * @param[out] ack    err OK with a payload, or NODATA with none (a hole the
   *                    download loop skips, or the end of the log).
   */
  void DataLog(uint32_t index, Ack &ack) const;

  /** @brief A backup-register word by index; 0 when out of range. */
  uint32_t BackupWord(uint32_t index) const;

  /**
   * @brief   Internal-flash bytes at an absolute address.
   * @return  false when the range is outside the captured image.
   */
  bool Flash(uint32_t addr, void *out, uint32_t len) const;

  /**
   * @brief   One external page through on-die ECC, as captured.
   *
   * @param[in]  page     Physical page index.
   * @param[out] data     The data area (page size from the record).
   * @param[out] verdict  "ok", "corrected", "uncorrectable" or "corrected8";
   *                      "blank" for a page in a block the capture skipped.
   * @return  false when the capture has no paged external flash.
   */
  bool EccPage(uint32_t page, std::vector<uint8_t> &data, std::string &verdict) const;

  /** @brief A string from manifest.json at top level, e.g. "captured_at". */
  std::string ManifestString(const std::string &key) const;

private:
  std::string dir_;
  IdentityRecord identity_;
  std::vector<uint8_t> flash_;
  uint32_t flash_base_ = 0x08000000U;
  std::vector<uint8_t> backup_;
  std::string uid_;
  uint32_t flash_kb_ = 0;
  std::map<std::string, std::string> manifest_strings_;
  std::vector<uint8_t> ecc_;
  std::map<uint32_t, std::pair<size_t, std::string>> ecc_index_; ///< page -> (row, verdict)
  uint32_t page_bytes_ = 0;
  std::unique_ptr<CaptureDecoder> decoder_;
};

/**
 * @brief   Rebuild a SQLite download from a capture directory.
 *
 * @details CaptureSource, then the unchanged SqliteTagLogWriter: the header,
 *          then the data log walked exactly as tag-dwnld walks it (a NODATA
 *          index below the count is a hole and is skipped). Info rows record
 *          the provenance: "source" = "capture", the capture directory and
 *          its capture time.
 *
 * @param[in]  capture_dir  A tag-capture directory.
 * @param[in]  db_path      SQLite file to create (replaced if it exists).
 * @param[out] records      Data-log records written; may be nullptr.
 * @param[out] error        Why it failed; may be nullptr.
 * @return  true when the file was written.
 */
bool RebuildSqliteFromCapture(const std::string &capture_dir,
                              const std::string &db_path, uint32_t *records,
                              std::string *error);

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_CAPTURESOURCE_H
