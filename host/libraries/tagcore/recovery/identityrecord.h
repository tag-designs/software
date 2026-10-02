/**
 * @file    identityrecord.h
 * @brief   Parse the tag identity record from a captured internal-flash image.
 *
 * @details The record (embedded/tags/common/core/inc/tag_identity.h) sits
 *          directly after the interrupt vectors, at McuMap::identity_offset:
 *          an 8-byte header ("TGID" magic, format version, total size) then
 *          TLV entries {u16 id, u16 len, value} ending in an 0xFFFF entry. It
 *          has no CRC; a record is accepted only when its magic, its declared
 *          size and its end entry all agree. The host-side counterpart of
 *          embedded/tools/decode_tag_identity.py, limited to what a capture
 *          needs to choose a loader and check it against the part.
 */

#ifndef TAGCORE_RECOVERY_IDENTITYRECORD_H
#define TAGCORE_RECOVERY_IDENTITYRECORD_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tagcore::recovery {

/**
 * @struct  IdentityRegion
 * @brief   One region entry: where a structure lives in internal flash.
 */
struct IdentityRegion {
  uint32_t start = 0;         ///< First byte.
  uint32_t end = 0;           ///< One past the last byte; 0 = to the persistent end.
  uint32_t record_size = 0;   ///< Bytes per record, or 0 for an untyped region.
  uint32_t record_count = 0;  ///< Records, or 0 = until an erased one.
  uint32_t layout_version = 0; ///< Layout version of the records.
};

/**
 * @struct  IdentityBackupState
 * @brief   Where the BackupState words live in the backup registers.
 */
struct IdentityBackupState {
  uint32_t base = 0;          ///< Address of word 0.
  uint32_t word_count = 0;    ///< Words in the mirror.
  uint32_t valid_magic = 0;   ///< Value of the valid word when retained state is valid.
  uint32_t word_valid = 0;    ///< Index of the valid word.
  uint32_t word_state = 0;    ///< Index of the state word.
  uint32_t word_pages = 0;    ///< Index of the internal page/checkpoint count.
  uint32_t word_external_blocks = 0; ///< Index of the external block count.
  uint32_t word_reset_cause = 0;     ///< Index of the reset cause.
};

/**
 * @struct  IdentityRecord
 * @brief   The fields of a tag identity record a capture uses.
 */
struct IdentityRecord {
  bool found = false;           ///< A well-formed record was present.
  std::string error;            ///< Why not, when !found.
  uint16_t format_version = 0;  ///< Record format version.
  std::map<std::string, std::string> strings; ///< target, board, loader, decoder, git_sha, ...
  bool has_external_flash = false; ///< The record describes an external part.
  uint32_t jedec_id = 0;        ///< Manufacturer << 16 | device, as the firmware knows it.
  uint32_t flash_size = 0;      ///< Bytes (data area).
  uint32_t program_page = 0;    ///< Program page, bytes.
  uint32_t erase_unit = 0;      ///< Erase unit, bytes.
  uint32_t spare = 0;           ///< Spare bytes per page (NAND), else 0.
  /// Regions by name: image, persistent, state_log, stored_config,
  /// data_headers, calibration, nand_map, scratchpad.
  std::map<std::string, IdentityRegion> regions;
  bool has_backup_state = false;   ///< backup_state was present.
  IdentityBackupState backup_state; ///< The BackupState map.
  bool has_numbers = false;        ///< numbers was present.
  uint32_t tag_type = 0;           ///< TagType value.
  float qtmonitor_min_version = 0; ///< As tag-info reports it.
  float accel_constant = 0;        ///< As tag-info reports it.
  float mag_constant = 0;          ///< As tag-info reports it.
  uint32_t tag_state_max = 0;      ///< Largest TagState value the firmware accepts.
  uint32_t state_event_max = 0;    ///< Largest State_Event value.
  bool has_session_facts = false;  ///< session_facts was present.
  uint32_t session_facts_offset = 0; ///< Offset of t_sessionFacts in the stored config.

  /** @brief A string field, or "" when absent. */
  std::string String(const std::string &name) const;
};

/**
 * @brief   Parse the record at @p offset in @p flash.
 *
 * @param[in] flash   Internal flash image, first byte at the flash base.
 * @param[in] offset  McuMap::identity_offset.
 * @return  The record; found is false, with error set, when it is absent or
 *          malformed (images built before the record existed have none).
 */
IdentityRecord ParseIdentityRecord(const std::vector<uint8_t> &flash,
                                   uint32_t offset);

} // namespace tagcore::recovery

#endif // TAGCORE_RECOVERY_IDENTITYRECORD_H
