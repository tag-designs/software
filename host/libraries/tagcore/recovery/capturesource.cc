/**
 * @file    capturesource.cc
 * @brief   Capture-backed rebuild of a download; contract in capturesource.h.
 *
 * @details The common parts (TagInfo, calibration, state history, session
 *          facts) follow the shared firmware code and the identity record.
 *          Each family supplies a CaptureDecoder for what is family-specific:
 *          its stored configuration (readConfig()), its external flash size
 *          as infoAck() reports it, its data-log count, and its data_logAck().
 */

#include "recovery/capturesource.h"

#include "sqlitelog.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

namespace tagcore::recovery {

namespace {

constexpr uint32_t kErased32 = 0xFFFFFFFFU;

bool ReadFile(const std::string &path, std::vector<uint8_t> &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool ReadText(const std::string &path, std::string &out) {
  std::ifstream in(path);
  if (!in)
    return false;
  std::ostringstream s;
  s << in.rdbuf();
  out = s.str();
  return true;
}

bool Fail(std::string *error, const std::string &why) {
  if (error)
    *error = why;
  return false;
}

uint32_t Le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t Le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

float LeF32(const uint8_t *p) {
  const uint32_t u = Le32(p);
  float f;
  std::memcpy(&f, &u, sizeof f);
  return f;
}

} // namespace

/**
 * @class   CaptureDecoder
 * @brief   The family-specific half of a capture rebuild.
 */
class CaptureDecoder {
public:
  virtual ~CaptureDecoder() = default;
  /** @brief The family's readConfig(), from the stored configuration. */
  virtual bool ReadConfig(const CaptureSource &src, Config &config,
                          std::string *error) const = 0;
  /** @brief externalFlashSize() as infoAck() reports it. */
  virtual uint64_t ExternalFlashSize(const CaptureSource &src) const = 0;
  /** @brief The Status count the download loop walks. */
  virtual uint32_t DataLogCount(const CaptureSource &src) const = 0;
  /** @brief The family's data_logAck(). */
  virtual void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const = 0;
};

namespace {

/**
 * @brief   IMUTag with SPI-NAND checkpoints (IMUTagNandBmp581).
 *
 * @details Follows embedded/tags/families/IMUTag/src/datalog.c and config.c,
 *          identical at fw-v0.0.3 and HEAD. See
 *          design/offline-log-reconstruction.md, "IMUTagNandBmp581".
 */
class ImuTagNandDecoder : public CaptureDecoder {
public:
  /** Pages per checkpoint row (IMUTAG_CHECKPOINT_PAGES). */
  static constexpr uint32_t kCheckpointPages = 8;
  /** Data bytes per NAND page, and so per IMUTagRawLog. */
  static constexpr uint32_t kPageBytes = 2048;
  /** Checkpoint row: t_InternalDataHeader. */
  static constexpr uint32_t kCheckpointBytes = 16;
  /** Usable (logical) blocks of the GD5F2GM7RE: GD5F_LOGICAL_BLOCK_COUNT. */
  static constexpr uint32_t kLogicalBlocks = 2008;

  bool ReadConfig(const CaptureSource &src, Config &config,
                  std::string *error) const override {
    // t_storedconfig (families/IMUTag/inc/config.h): int32 start @0, int32
    // stop @4, uint32 start_delay @8, u16 odr @12, u8 accel_range @14, u8
    // gyro_range @15. readConfig() drops start and stop.
    const auto it = src.Identity().regions.find("stored_config");
    if (it == src.Identity().regions.end())
      return Fail(error, "identity record has no stored_config region");
    uint8_t c[16];
    if (!src.Flash(it->second.start, c, sizeof c))
      return Fail(error, "stored configuration outside the captured image");
    config.Clear();
    config.set_tag_type(IMUTAG);
    config.set_start_delay(Le32(c + 8));
    Lsm6dsv *lsm6 = config.mutable_lsm6();
    lsm6->set_odr(static_cast<Lsm6dsv_ODR>(Le16(c + 12)));
    lsm6->set_accel_rng(static_cast<Lsm6dsv_ACCEL>(c[14]));
    lsm6->set_gyro_rng(static_cast<Lsm6dsv_GYRO>(c[15]));
    return true;
  }

  uint64_t ExternalFlashSize(const CaptureSource &src) const override {
    // externalFlashSize() = GD5F_BLOCK_SIZE * GD5F_LOGICAL_BLOCK_COUNT. The
    // record carries the block size (erase_unit) but not the logical count.
    return uint64_t(src.Identity().erase_unit) * kLogicalBlocks;
  }

  uint32_t DataLogCount(const CaptureSource &src) const override {
    return src.BackupWord(src.Identity().backup_state.word_external_blocks);
  }

  /** @brief readCheckpointForPage(): the checkpoint row covering @p index. */
  bool Checkpoint(const CaptureSource &src, uint32_t index, uint8_t row[16]) const {
    const IdentityRecord &id = src.Identity();
    const auto it = id.regions.find("data_headers");
    if (it == id.regions.end())
      return false;
    const uint32_t count = src.BackupWord(id.backup_state.word_pages);
    if (count == 0)
      return false;
    uint32_t candidate = index / kCheckpointPages;
    if (candidate > count - 1)
      candidate = count - 1;
    for (;;) {
      if (!src.Flash(it->second.start + candidate * kCheckpointBytes, row, kCheckpointBytes))
        return false;
      if (Le32(row) == kErased32)
        return false;
      const uint32_t logical_next = Le32(row + 8);
      if (logical_next <= index && index - logical_next < kCheckpointPages)
        return true;
      if (candidate == 0)
        return false;
      candidate--;
    }
  }

  void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const override {
    ack.Clear();
    ack.set_err(Ack_Err_NODATA);
    if (index >= DataLogCount(src) ||
        uint64_t(index + 1) * kPageBytes > ExternalFlashSize(src))
      return;
    uint8_t row[16];
    if (!Checkpoint(src, index, row))
      return;
    const uint32_t logical_next = Le32(row + 8);
    const uint32_t physical = Le32(row + 12) + (index - logical_next);
    uint16_t header_millis = Le16(row + 4);
    if (index != logical_next)
      header_millis &= 0x3FFU;   // flags travel on a group's first page only

    std::vector<uint8_t> page;
    std::string verdict;
    if (!src.EccPage(physical, page, verdict) || page.size() < kPageBytes)
      return;
    // dataLogReadPhysicalPage(): ECC OK or corrected, and a written header.
    if (verdict == "uncorrectable" || Le32(page.data()) == kErased32)
      return;
    IMUTagRawLog *log = ack.mutable_imu_raw_data_log();
    log->set_epoch(static_cast<int32_t>(Le32(page.data())));
    log->set_millisecond((Le16(page.data() + 4) & 0x3FFU) | (header_millis & ~0x3FFU));
    log->set_temperature(static_cast<int16_t>(Le16(page.data() + 6)) * 0.01f);
    log->set_samples(page.data(), kPageBytes);
    ack.set_err(Ack_Err_OK);
  }
};

} // namespace

CaptureSource::CaptureSource() = default;
CaptureSource::~CaptureSource() = default;

uint32_t CaptureSource::BackupWord(uint32_t index) const {
  if (uint64_t(index) * 4 + 4 > backup_.size())
    return 0;
  return Le32(backup_.data() + index * 4);
}

bool CaptureSource::Flash(uint32_t addr, void *out, uint32_t len) const {
  if (addr < flash_base_ || uint64_t(addr - flash_base_) + len > flash_.size())
    return false;
  std::memcpy(out, flash_.data() + (addr - flash_base_), len);
  return true;
}

bool CaptureSource::EccPage(uint32_t page, std::vector<uint8_t> &data,
                            std::string &verdict) const {
  if (page_bytes_ == 0)
    return false;
  const auto it = ecc_index_.find(page);
  if (it == ecc_index_.end()) {
    // A block the capture skipped as blank reads as erased.
    data.assign(page_bytes_, 0xFF);
    verdict = "blank";
    return true;
  }
  const size_t off = it->second.first * page_bytes_;
  if (off + page_bytes_ > ecc_.size())
    return false;
  data.assign(ecc_.begin() + off, ecc_.begin() + off + page_bytes_);
  verdict = it->second.second;
  return true;
}

std::string CaptureSource::ManifestString(const std::string &key) const {
  const auto it = manifest_strings_.find(key);
  return it == manifest_strings_.end() ? std::string() : it->second;
}

bool CaptureSource::Open(const std::string &dir, std::string *error) {
  dir_ = dir;
  if (!ReadFile(dir + "/internal_flash.bin", flash_))
    return Fail(error, "no internal_flash.bin in " + dir);
  if (!ReadFile(dir + "/backup_regs.bin", backup_))
    return Fail(error, "no backup_regs.bin in " + dir);

  std::string text;
  if (!ReadText(dir + "/manifest.json", text))
    return Fail(error, "no manifest.json in " + dir);
  google::protobuf::Struct manifest;
  if (!google::protobuf::util::JsonStringToMessage(text, &manifest).ok())
    return Fail(error, "manifest.json does not parse");
  for (const auto &kv : manifest.fields())
    if (kv.second.has_string_value())
      manifest_strings_[kv.first] = kv.second.string_value();
  uint32_t uid[3] = {0, 0, 0};
  bool have_uid[3] = {false, false, false};
  const auto regs = manifest.fields().find("registers");
  if (regs != manifest.fields().end())
    for (const auto &v : regs->second.list_value().values()) {
      const auto &f = v.struct_value().fields();
      const std::string name = f.count("name") ? f.at("name").string_value() : "";
      const uint32_t value = f.count("value")
          ? static_cast<uint32_t>(std::stoul(f.at("value").string_value(), nullptr, 0))
          : 0;
      if (name == "FLASH_SIZE")
        flash_kb_ = value & 0xFFFFU;
      for (int i = 0; i < 3; i++)
        if (name == "UID" + std::to_string(i)) {
          uid[i] = value;
          have_uid[i] = true;
        }
    }
  if (have_uid[0] && have_uid[1] && have_uid[2]) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%08X%08X%08X", uid[2], uid[1], uid[0]);
    uid_ = buf;
  }

  // The record sits after the vectors: 0x1A0 on the L432, 0x240 on the U375.
  for (uint32_t off : {0x1A0U, 0x240U}) {
    identity_ = ParseIdentityRecord(flash_, off);
    if (identity_.found)
      break;
  }
  if (!identity_.found)
    return Fail(error, "no identity record in the captured image (" +
                           identity_.error + "); fw-v0.0.3 images need the "
                           "release layout, which is not implemented yet");
  if (!identity_.has_backup_state)
    return Fail(error, "identity record has no backup_state map");

  // External flash through ECC, indexed by physical page from pages.csv.
  if (ReadFile(dir + "/external_ecc.bin", ecc_)) {
    std::string csv;
    if (!ReadText(dir + "/external_pages.csv", csv))
      return Fail(error, "external_ecc.bin without external_pages.csv");
    page_bytes_ = identity_.program_page;
    const uint32_t stored_page = identity_.program_page + identity_.spare;
    std::istringstream lines(csv);
    std::string line;
    std::getline(lines, line); // header
    size_t row = 0;
    while (std::getline(lines, line)) {
      if (line.empty())
        continue;
      std::istringstream f(line);
      std::string page, block, raw_c0, ecc_c0, ecc_f0, verdict;
      std::getline(f, page, ',');
      std::getline(f, block, ',');
      std::getline(f, raw_c0, ',');
      std::getline(f, ecc_c0, ',');
      std::getline(f, ecc_f0, ',');
      std::getline(f, verdict, ',');
      ecc_index_[static_cast<uint32_t>(std::stoul(page))] = {row++, verdict};
    }
    // ecc_ holds whole stored pages (data + spare); keep the data areas only.
    std::vector<uint8_t> data;
    data.reserve(row * page_bytes_);
    for (size_t r = 0; r < row && (r + 1) * stored_page <= ecc_.size(); r++)
      data.insert(data.end(), ecc_.begin() + r * stored_page,
                  ecc_.begin() + r * stored_page + page_bytes_);
    ecc_.swap(data);
  }

  const std::string decoder = identity_.String("decoder");
  if (decoder == "imutag" && page_bytes_ != 0)
    decoder_ = std::make_unique<ImuTagNandDecoder>();
  else
    return Fail(error, "capture rebuild not implemented for decoder \"" + decoder +
                           "\"" + (page_bytes_ ? "" : " (or no external flash captured)"));
  return true;
}

bool CaptureSource::Header(TagLogHeader &header, std::string *error) const {
  header = TagLogHeader();
  if (!decoder_)
    return Fail(error, "capture not open");
  if (!decoder_->ReadConfig(*this, header.config, error))
    return false;

  // TagInfo, as infoAck() fills it (common/core/src/monitor.c).
  TagInfo &info = header.info;
  const IdentityRecord &id = identity_;
  info.set_tag_type(static_cast<TagType>(id.tag_type));
  info.set_board_desc(id.String("board_desc"));
  info.set_uuid(uid_);
  info.set_intflashsz(flash_kb_);
  info.set_extflashsz(static_cast<int64_t>(decoder_->ExternalFlashSize(*this)));
  info.set_firmware(id.String("firmware"));
  info.set_gitrepo(id.String("git_repo"));
  info.set_githash(id.String("git_hash"));
  info.set_source_path(id.String("source_path"));
  info.set_build_time(id.String("git_date"));
  info.set_qtmonitor_min_version(id.qtmonitor_min_version);
  info.set_accelconstant(id.accel_constant);
  info.set_magconstant(id.mag_constant);
  // The RV3028 offset as stored with the session (B2). Live, infoAck() falls
  // back to an I2C read; a capture has no such fallback, so an invalid or
  // erased record leaves the field unset.
  const auto sc = id.regions.find("stored_config");
  if (id.has_session_facts && sc != id.regions.end()) {
    uint8_t f[16];
    if (Flash(sc->second.start + id.session_facts_offset, f, sizeof f) &&
        Le32(f) == 1U && (Le16(f + 6) & 1U))
      info.set_ppm_clock_error(LeF32(f + 8));
  }

  // Calibration slots, as read_calibration(index) serves them.
  const auto cal = id.regions.find("calibration");
  if (cal != id.regions.end() && cal->second.record_size >= 56)
    for (uint32_t i = 0; i < cal->second.record_count; i++) {
      uint8_t slot[56];
      if (!Flash(cal->second.start + i * cal->second.record_size, slot, sizeof slot) ||
          Le32(slot) == kErased32)
        break;
      CalibrationConstants c;
      c.set_timestamp(static_cast<int32_t>(Le32(slot)));
      CalibrationConstants_MagConstants *m = c.mutable_magnetometer();
      float v[13];
      for (int k = 0; k < 13; k++)
        v[k] = LeF32(slot + 4 + 4 * k);
      m->set_b(v[0]);
      m->set_v0(v[1]);
      m->set_v1(v[2]);
      m->set_v2(v[3]);
      m->set_a00(v[4]);
      m->set_a01(v[5]);
      m->set_a02(v[6]);
      m->set_a10(v[7]);
      m->set_a11(v[8]);
      m->set_a12(v[9]);
      m->set_a20(v[10]);
      m->set_a21(v[11]);
      m->set_a22(v[12]);
      header.calibration.push_back(c);
    }

  // State history, as system_logAck() serves it: t_StateMarker records.
  const auto sl = id.regions.find("state_log");
  if (sl != id.regions.end() && sl->second.record_size >= 24) {
    const uint32_t rs = sl->second.record_size;
    const bool u3 = rs >= 32; // U3 markers carry a detail word
    for (uint32_t i = 0; i < sl->second.record_count; i++) {
      std::vector<uint8_t> r(rs);
      if (!Flash(sl->second.start + i * rs, r.data(), rs))
        break;
      const int32_t epoch = static_cast<int32_t>(Le32(r.data()));
      const uint32_t state = r[4];
      const uint32_t reason = r[20];
      if (epoch == -1 || state == 0 || state > id.tag_state_max ||
          reason > id.state_event_max)
        break;
      State s;
      Status *st = s.mutable_status();
      st->set_millis(int64_t(epoch) * 1000);
      st->set_state(static_cast<TagState>(state));
      st->set_internal_data_count(static_cast<int32_t>(Le32(r.data() + 8)));
      st->set_external_data_count(static_cast<int32_t>(Le32(r.data() + 12)));
      st->set_voltage(Le16(r.data() + 16) * 0.01f);
      st->set_temperature(static_cast<int16_t>(Le16(r.data() + 18)) * 0.1f);
      s.set_transition_reason(static_cast<State_Event>(reason));
      if (u3)
        s.set_transition_detail(Le32(r.data() + 24));
      header.states.push_back(s);
    }
  }

  return true;
}

uint32_t CaptureSource::DataLogCount() const {
  return decoder_ ? decoder_->DataLogCount(*this) : 0;
}

void CaptureSource::DataLog(uint32_t index, Ack &ack) const {
  if (!decoder_) {
    ack.Clear();
    ack.set_err(Ack_Err_NODATA);
    return;
  }
  decoder_->DataLog(*this, index, ack);
}

bool RebuildSqliteFromCapture(const std::string &capture_dir,
                              const std::string &db_path, uint32_t *records,
                              std::string *error) {
  CaptureSource src;
  if (!src.Open(capture_dir, error))
    return false;
  TagLogHeader header;
  if (!src.Header(header, error))
    return false;
  SqliteTagLogWriter writer(db_path, header.config, true);
  if (!writer.isOpen())
    return Fail(error, writer.lastError());
  if (!writer.writeHeader(header) || !writer.writeInfo("source", "capture") ||
      !writer.writeInfo("capture_dir", capture_dir) ||
      !writer.writeInfo("captured_at", src.ManifestString("captured_at")))
    return Fail(error, writer.lastError());
  if (!writer.beginLog())
    return Fail(error, writer.lastError());

  // The download loop of host/commandline/dwnld.cc.
  const uint32_t max_count = src.DataLogCount();
  uint32_t total = 0;
  int len = 0;
  Ack ack;
  do {
    src.DataLog(total, ack);
    if (ack.err() == Ack_Err_NODATA && total < max_count) {
      total++;     // a hole: skip it
      len = 1;
      continue;
    }
    len = writer.writeLog(ack);
    if (len < 0)
      return Fail(error, "writeLog failed at index " + std::to_string(total) +
                             ": " + writer.lastError());
    total += static_cast<uint32_t>(len);
    if (records)
      *records += static_cast<uint32_t>(len);
  } while (len != 0);
  if (!writer.endLog())
    return Fail(error, writer.lastError());
  return true;
}

} // namespace tagcore::recovery
