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
constexpr uint16_t kNumbers = 0x0201U;
constexpr uint16_t kBackupState = 0x0401U;
constexpr uint16_t kSessionFacts = 0x0601U;
constexpr uint16_t kDefaultConfig = 0x0309U;

/// Region entries: id -> name, as in decode_tag_identity.py.
const std::map<uint16_t, const char *> &RegionIds() {
  static const std::map<uint16_t, const char *> ids = {
      {0x0301, "image"},        {0x0302, "persistent"}, {0x0303, "state_log"},
      {0x0304, "stored_config"}, {0x0305, "data_headers"}, {0x0306, "calibration"},
      {0x0307, "nand_map"},     {0x0308, "scratchpad"},
  };
  return ids;
}

float F32(const std::vector<uint8_t> &b, size_t o);

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

float F32(const std::vector<uint8_t> &b, size_t o) {
  const uint32_t u = U32(b, o);
  float f;
  std::memcpy(&f, &u, sizeof f);
  return f;
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
    } else if (RegionIds().count(id) && len >= 20) {
      IdentityRegion g;
      g.start = U32(flash, v);
      g.end = U32(flash, v + 4);
      g.record_size = U32(flash, v + 8);
      g.record_count = U32(flash, v + 12);
      g.layout_version = U32(flash, v + 16);
      r.regions[RegionIds().at(id)] = g;
    } else if (id == kNumbers && len >= 24) {
      r.has_numbers = true;
      r.tag_type = U32(flash, v);
      r.qtmonitor_min_version = F32(flash, v + 4);
      r.accel_constant = F32(flash, v + 8);
      r.mag_constant = F32(flash, v + 12);
      r.tag_state_max = U32(flash, v + 16);
      r.state_event_max = U32(flash, v + 20);
    } else if (id == kBackupState && len >= 32) {
      r.has_backup_state = true;
      IdentityBackupState &b = r.backup_state;
      b.base = U32(flash, v);
      b.word_count = U32(flash, v + 4);
      b.valid_magic = U32(flash, v + 8);
      b.word_valid = U32(flash, v + 12);
      b.word_state = U32(flash, v + 16);
      b.word_pages = U32(flash, v + 20);
      b.word_external_blocks = U32(flash, v + 24);
      b.word_reset_cause = U32(flash, v + 28);
    } else if (id == kDefaultConfig && len >= 8) {
      r.has_default_config = true;
      r.default_config_addr = U32(flash, v);
      r.default_config_len_addr = U32(flash, v + 4);
    } else if (id == kSessionFacts && len >= 12) {
      r.has_session_facts = true;
      r.session_facts_offset = U32(flash, v);
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
