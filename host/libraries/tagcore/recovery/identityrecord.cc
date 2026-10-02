/**
 * @file    identityrecord.cc
 * @brief   Identity-record parser; contract in identityrecord.h.
 */

#include "recovery/identityrecord.h"

#include <cstring>

namespace tagcore::recovery {

namespace {

constexpr uint32_t kMagic = 0x44494754U; // "TGID"
constexpr uint16_t kEnd = 0xFFFFU;
constexpr uint16_t kExternalFlash = 0x0402U;

/// String entries: id -> name, as in decode_tag_identity.py.
const std::map<uint16_t, const char *> &StringIds() {
  static const std::map<uint16_t, const char *> ids = {
      {0x0101, "target"},     {0x0102, "family"},      {0x0103, "board"},
      {0x0104, "board_desc"}, {0x0105, "board_revision"}, {0x0106, "firmware"},
      {0x0107, "git_repo"},   {0x0108, "git_hash"},    {0x0109, "git_sha"},
      {0x010A, "git_date"},   {0x010B, "source_path"}, {0x010C, "flash_part"},
      {0x010D, "rtc_part"},   {0x010E, "loader"},      {0x010F, "decoder"},
      {0x0110, "monitor_version"},
  };
  return ids;
}

uint16_t U16(const std::vector<uint8_t> &b, size_t o) {
  return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

uint32_t U32(const std::vector<uint8_t> &b, size_t o) {
  return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
         (static_cast<uint32_t>(b[o + 2]) << 16) |
         (static_cast<uint32_t>(b[o + 3]) << 24);
}

IdentityRecord Fail(const std::string &why) {
  IdentityRecord r;
  r.error = why;
  return r;
}

} // namespace

std::string IdentityRecord::String(const std::string &name) const {
  const auto it = strings.find(name);
  return it == strings.end() ? std::string() : it->second;
}

IdentityRecord ParseIdentityRecord(const std::vector<uint8_t> &flash,
                                   uint32_t offset) {
  if (uint64_t(offset) + 8 > flash.size())
    return Fail("image too short for an identity record");
  if (U32(flash, offset) != kMagic)
    return Fail("no identity record (magic not found)");
  IdentityRecord r;
  r.format_version = U16(flash, offset + 4);
  const uint32_t size = U16(flash, offset + 6);
  if (size < 12 || uint64_t(offset) + size > flash.size())
    return Fail("identity record size out of range");

  uint32_t off = 8;
  while (off + 4 <= size) {
    const uint16_t id = U16(flash, offset + off);
    const uint16_t len = U16(flash, offset + off + 2);
    off += 4;
    if (id == kEnd) {
      if (off != size)
        return Fail("identity record end entry does not match its size");
      r.found = true;
      return r;
    }
    if (len % 4 != 0 || off + len > size)
      return Fail("identity record entry has a bad length");
    const size_t v = offset + off;
    const auto sid = StringIds().find(id);
    if (sid != StringIds().end()) {
      const char *p = reinterpret_cast<const char *>(flash.data() + v);
      r.strings[sid->second] = std::string(p, strnlen(p, len));
    } else if (id == kExternalFlash && len >= 20) {
      r.has_external_flash = true;
      r.jedec_id = U32(flash, v);
      r.flash_size = U32(flash, v + 4);
      r.program_page = U32(flash, v + 8);
      r.erase_unit = U32(flash, v + 12);
      r.spare = U32(flash, v + 16);
    }
    off += len;
  }
  return Fail("identity record has no end entry");
}

} // namespace tagcore::recovery
