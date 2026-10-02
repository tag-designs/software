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
