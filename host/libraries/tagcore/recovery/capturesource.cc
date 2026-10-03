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

#include "recovery/sha256.h"
#include "uiuctag_log_format.h"
#include "sqlitelog.h"

#include <cmath>
#include <cstdio>
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

/** @brief Parse an unsigned number (decimal, or 0x hex); false on junk. */
bool ParseU32(const std::string &text, uint32_t &value) {
  try {
    size_t used = 0;
    const unsigned long v = std::stoul(text, &used, 0);
    if (used != text.size())
      return false;
    value = static_cast<uint32_t>(v);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

/**
 * @brief   Records a region can hold: its record_count, or when that is 0
 *          ("until an erased one") as many as fit before its end, which is
 *          itself 0 for "to the end of the persistent region".
 */
uint32_t RegionRecords(const IdentityRecord &id, const IdentityRegion &r) {
  if (r.record_size == 0)
    return 0;
  if (r.record_count != 0)
    return r.record_count;
  uint32_t end = r.end;
  if (end == 0) {
    const auto p = id.regions.find("persistent");
    end = p == id.regions.end() ? 0 : p->second.end;
  }
  return end > r.start ? (end - r.start) / r.record_size : 0;
}

/**
 * @struct  KnownLayout
 * @brief   One region layout a decoder was written against.
 */
struct KnownLayout {
  const char *region;    ///< Region name in the identity record.
  uint32_t version;      ///< Its layout_version.
  uint32_t record_size;  ///< Its record_size; 0 = any.
};

/**
 * @brief   Refuse a capture whose region layouts this host does not know.
 *
 * @details The decoders hard-code the layouts they were written against. A
 *          firmware that changes one is expected to bump the region's
 *          layout_version (the firmware asserts the offsets these decoders
 *          read; see tag_identity.c), so an unknown (version, size) pair
 *          means the decoder is out of date. Better refused than misread.
 *          A region the record does not carry is not checked here.
 */
bool CheckLayouts(const IdentityRecord &id, const std::vector<KnownLayout> &known,
                  std::string *error) {
  std::map<std::string, std::string> seen;
  for (const KnownLayout &k : known) {
    const auto it = id.regions.find(k.region);
    if (it == id.regions.end())
      continue;
    const IdentityRegion &r = it->second;
    if (r.layout_version == k.version && (k.record_size == 0 || r.record_size == k.record_size))
      seen[k.region] = "ok";
    else if (!seen.count(k.region))
      seen[k.region] = "layout v" + std::to_string(r.layout_version) + ", " +
                       std::to_string(r.record_size) + "-byte records";
  }
  for (const auto &kv : seen)
    if (kv.second != "ok")
      return Fail(error, "region " + kv.first + " has " + kv.second +
                             ", which this host's decoder does not know; "
                             "update host/libraries/tagcore/recovery/capturesource.cc");
  return true;
}

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
  /** @brief The region layouts this decoder reads, for CheckLayouts(). */
  virtual std::vector<KnownLayout> Layouts() const = 0;
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

uint32_t StatusCount(const CaptureSource &src);

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

  std::vector<KnownLayout> Layouts() const override {
    // stored_config v2 appended session facts after the members read here.
    return {{"stored_config", 1, 0}, {"stored_config", 2, 0},
            {"data_headers", 1, kCheckpointBytes}};
  }

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
    return StatusCount(src);
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
    // Bounded by external_blocks alone, as the firmware is; the download
    // loop's own count (StatusCount()) falls back to the internal count.
    const uint32_t external =
        src.BackupWord(src.Identity().backup_state.word_external_blocks);
    if (index >= external ||
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

/**
 * @brief   The download count tag-dwnld walks: Status external_data_count,
 *          or internal_data_count when that is zero (dwnld.cc).
 */
uint32_t StatusCount(const CaptureSource &src) {
  const IdentityBackupState &b = src.Identity().backup_state;
  const uint32_t external = src.BackupWord(b.word_external_blocks);
  return external != 0 ? external : src.BackupWord(b.word_pages);
}

/**
 * @brief   PresTag with converted samples (decoder "prestag").
 *
 * @details Follows embedded/tags/families/PresTag/src/datalog.c and config.c.
 *          One page is a t_DataHeader in internal flash (vddHeader[index]:
 *          int32 epoch, uint16 vdd100[2], the second being temperature) plus
 *          60 raw LPS27 samples (int16 pressure, int16 temperature) at
 *          index * 240 in external flash. A page ends at its first pressure of
 *          -1. The tag converts on the way out: pressure / 16 hPa, temperature
 *          / 100 C, header fields * 0.01.
 */
class PresTagDecoder : public CaptureDecoder {
public:
  static constexpr uint32_t kSamples = 60;          ///< PRESTAG_LOG_SAMPLES
  static constexpr uint32_t kPageBytes = kSamples * 4; ///< sizeof(t_PresTagDataLog)
  static constexpr uint32_t kHeaderBytes = 8;       ///< sizeof(t_DataHeader)

  std::vector<KnownLayout> Layouts() const override {
    return {{"stored_config", 1, 0}, {"stored_config", 2, 0},
            {"data_headers", 1, kHeaderBytes}};
  }

  bool ReadConfig(const CaptureSource &src, Config &config,
                  std::string *error) const override {
    // t_storedconfig (families/PresTag/inc/config.h): int32 start @0, int32
    // stop @4, hibernate_t hibernate[2] @8 (int32 start_epoch, end_epoch),
    // uint32 lps_period @24.
    const auto it = src.Identity().regions.find("stored_config");
    if (it == src.Identity().regions.end())
      return Fail(error, "identity record has no stored_config region");
    uint8_t c[28];
    if (!src.Flash(it->second.start, c, sizeof c))
      return Fail(error, "stored configuration outside the captured image");
    config.Clear();
    config.set_tag_type(PRESTAG);
    config.set_period(Le32(c + 24));
    Config_Interval *active = config.mutable_active_interval();
    active->set_start_epoch(static_cast<int32_t>(Le32(c)));
    active->set_end_epoch(static_cast<int32_t>(Le32(c + 4)));
    for (int i = 0; i < 2; i++) {
      Config_Interval *h = config.add_hibernate();
      h->set_start_epoch(static_cast<int32_t>(Le32(c + 8 + 8 * i)));
      h->set_end_epoch(static_cast<int32_t>(Le32(c + 12 + 8 * i)));
    }
    return true;
  }

  uint64_t ExternalFlashSize(const CaptureSource &src) const override {
    return src.Identity().flash_size;
  }

  uint32_t DataLogCount(const CaptureSource &src) const override {
    return StatusCount(src);
  }

  void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const override {
    // Invalid indices answer err OK with no payload, which ends the download.
    ack.Clear();
    ack.set_err(Ack_Err_OK);
    const IdentityRecord &id = src.Identity();
    const auto dh = id.regions.find("data_headers");
    const auto ps = id.regions.find("persistent");
    if (dh == id.regions.end() || ps == id.regions.end())
      return;
    const uint32_t end = dh->second.end ? dh->second.end : ps->second.end;
    const uint64_t address = uint64_t(dh->second.start) + uint64_t(index) * kHeaderBytes;
    const uint64_t byte_offset = uint64_t(kPageBytes) * index;
    uint8_t h[kHeaderBytes];
    if (address + kHeaderBytes > end ||
        !src.Flash(static_cast<uint32_t>(address), h, sizeof h) ||
        Le32(h) == kErased32 || byte_offset >= ExternalFlashSize(src))
      return;
    // readExternalPage(): a page cut short by the end of the part reads FFh.
    uint8_t page[kPageBytes];
    std::memset(page, 0xFF, sizeof page);
    uint32_t count = kPageBytes;
    if (byte_offset + count > ExternalFlashSize(src))
      count = static_cast<uint32_t>(ExternalFlashSize(src) - byte_offset);
    if (!src.External(byte_offset, page, count))
      return;
    PresTagLog *log = ack.mutable_prestag_data_log();
    log->set_epoch(static_cast<int32_t>(Le32(h)));
    log->set_voltage(Le16(h + 4) * 0.01f);
    log->set_temperature(Le16(h + 6) * 0.01f);
    for (uint32_t j = 0; j < kSamples; j++) {
      const int16_t pressure = static_cast<int16_t>(Le16(page + 4 * j));
      if (pressure == -1)
        break;
      PresTagLog_PT *d = log->add_data();
      d->set_pressure(pressure / 16.0f);
      d->set_temperature(static_cast<int16_t>(Le16(page + 4 * j + 2)) / 100.0f);
    }
  }
};

/**
 * @brief   CompassTag (decoder "compasstag").
 *
 * @details Follows embedded/tags/families/CompassTag/src/datalog.c and
 *          config.c. Header i (vddHeader[i]: int32 epoch, uint16 vdd100,
 *          int16 temp10) owns the 380 external bytes at i * 380: ten blocks,
 *          each three RawSensorData samples (six int16: ax ay az mx my mz)
 *          and a uint16 activity word packing a 5-bit field per sample,
 *          LSB first. A block whose activity word is FFFFh is unfinished and
 *          skipped, not an end. The tag converts on the way out: acceleration
 *          * 0.976 mg, field * 0.04 uT, activity * 100 / 30 percent.
 */
class CompassTagDecoder : public CaptureDecoder {
public:
  static constexpr uint32_t kBlocks = 10;          ///< DATALOG_SAMPLES
  static constexpr uint32_t kSamplesPerBlock = 3;  ///< SAMPLES_PER_BLOCK
  static constexpr uint32_t kActivityBits = 5;     ///< ACTIVITY_BITS_PER_SAMPLE
  static constexpr int32_t kPeriodS = 30;          ///< COMPASS_SAMPLE_PERIOD_S
  static constexpr uint32_t kBlockBytes = kSamplesPerBlock * 12 + 2;
  static constexpr uint32_t kPageBytes = kBlocks * kBlockBytes; ///< sizeof(t_DataLog)
  static constexpr uint32_t kHeaderBytes = 8;      ///< sizeof(t_DataHeader)

  std::vector<KnownLayout> Layouts() const override {
    return {{"stored_config", 1, 0}, {"stored_config", 2, 0},
            {"data_headers", 1, kHeaderBytes}};
  }

  bool ReadConfig(const CaptureSource &src, Config &config,
                  std::string *error) const override {
    // t_storedconfig (families/CompassTag/inc/config.h): int32 start @0,
    // int32 stop @4, hibernate_t hibernate[2] @8.
    const auto it = src.Identity().regions.find("stored_config");
    if (it == src.Identity().regions.end())
      return Fail(error, "identity record has no stored_config region");
    uint8_t c[24];
    if (!src.Flash(it->second.start, c, sizeof c))
      return Fail(error, "stored configuration outside the captured image");
    config.Clear();
    config.set_tag_type(COMPASSTAG);
    Config_Interval *active = config.mutable_active_interval();
    active->set_start_epoch(static_cast<int32_t>(Le32(c)));
    active->set_end_epoch(static_cast<int32_t>(Le32(c + 4)));
    for (int i = 0; i < 2; i++) {
      Config_Interval *h = config.add_hibernate();
      h->set_start_epoch(static_cast<int32_t>(Le32(c + 8 + 8 * i)));
      h->set_end_epoch(static_cast<int32_t>(Le32(c + 12 + 8 * i)));
    }
    return true;
  }

  uint64_t ExternalFlashSize(const CaptureSource &src) const override {
    return src.Identity().flash_size;
  }

  uint32_t DataLogCount(const CaptureSource &src) const override {
    return StatusCount(src);
  }

  void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const override {
    // Invalid indices answer err OK with no payload, which ends the download.
    ack.Clear();
    ack.set_err(Ack_Err_OK);
    const IdentityRecord &id = src.Identity();
    const auto dh = id.regions.find("data_headers");
    const auto ps = id.regions.find("persistent");
    if (dh == id.regions.end() || ps == id.regions.end())
      return;
    const uint32_t end = dh->second.end ? dh->second.end : ps->second.end;
    const uint64_t address = uint64_t(dh->second.start) + uint64_t(index) * kHeaderBytes;
    const uint64_t byte_offset = uint64_t(kPageBytes) * index;
    uint8_t h[kHeaderBytes];
    if (address + kHeaderBytes > end ||
        !src.Flash(static_cast<uint32_t>(address), h, sizeof h) ||
        Le32(h) == kErased32 || byte_offset >= ExternalFlashSize(src))
      return;
    // readExternalPage(): a page cut short by the end of the part reads FFh.
    uint8_t page[kPageBytes];
    std::memset(page, 0xFF, sizeof page);
    uint32_t count = kPageBytes;
    if (byte_offset + count > ExternalFlashSize(src))
      count = static_cast<uint32_t>(ExternalFlashSize(src) - byte_offset);
    if (!src.External(byte_offset, page, count))
      return;
    CompassTagLog *log = ack.mutable_compasstag_data_log();
    log->set_epoch(static_cast<int32_t>(Le32(h)));
    log->set_voltage(Le16(h + 4) * 0.01f);
    log->set_temperature(static_cast<int16_t>(Le16(h + 6)) * 0.1f);
    log->set_sample_period_s(kPeriodS);
    for (uint32_t b = 0; b < kBlocks; b++) {
      const uint8_t *block = page + b * kBlockBytes;
      const uint16_t activity = Le16(block + kSamplesPerBlock * 12);
      if (activity == 0xFFFFU)
        continue;
      for (uint32_t j = 0; j < kSamplesPerBlock; j++) {
        const uint8_t *r = block + j * 12;
        auto raw = [r](int k) { return static_cast<int16_t>(Le16(r + 2 * k)); };
        CompassTagLog_Compass *d = log->add_data();
        const int field = (activity >> (j * kActivityBits)) & ((1 << kActivityBits) - 1);
        d->set_activity(field * 100.0f / kPeriodS);
        d->set_ax(raw(0) * 0.976f);
        d->set_ay(raw(1) * 0.976f);
        d->set_az(raw(2) * 0.976f);
        d->set_mx(raw(3) * 0.04f);
        d->set_my(raw(4) * 0.04f);
        d->set_mz(raw(5) * 0.04f);
      }
    }
  }
};

/**
 * @brief   UIUCTag (decoder "uiuctag").
 *
 * @details Follows embedded/tags/UIUCTag/src/datalog.c and config.c, with the
 *          layout in include/uiuctag_log_format.h. Checkpoint i (vddHeader[i]:
 *          int32 epoch of slot 0, uint16 vdd100, uint16 extern_log_block)
 *          names a 288-byte external block of 24 time-indexed 12-byte slots
 *          {float pressure, float temperature, uint32 activity}. Slots are
 *          placed by time, so a missed sample leaves an erased slot rather
 *          than moving later ones. The block goes out raw: trailing erased
 *          slots trimmed, interior gaps kept. Indices at or above the
 *          checkpoint count, or with an erased checkpoint, answer NODATA.
 */
class UiucTagDecoder : public CaptureDecoder {
public:
  static constexpr uint32_t kSlots = 24;          ///< UIUCTAG_LOG_SAMPLES
  static constexpr uint32_t kSlotBytes = 12;      ///< UIUCTAG_SAMPLE_SIZE
  static constexpr uint32_t kBlockBytes = kSlots * kSlotBytes; ///< DATALOG_BLOCK_BYTES
  static constexpr uint32_t kHeaderBytes = 8;     ///< UIUCTAG_INTERNAL_LOG_SIZE
  /** UIUCTAG_ADXL_INACT_THRESH_MG: what readConfig() reports, hardcoded. */
  static constexpr uint32_t kInactThreshMg = 1100;

  std::vector<KnownLayout> Layouts() const override {
    return {{"stored_config", 1, 0}, {"stored_config", 2, 0},
            {"data_headers", 1, kHeaderBytes}};
  }

  bool ReadConfig(const CaptureSource &src, Config &config,
                  std::string *error) const override {
    // t_storedconfig (families/BitPresTag/inc/config.h): u16
    // adxl_act_thresh_cnt @0, u16 adxl_inact_thresh_cnt @2, u16
    // adxl_inactive_samples @4, u8 filter @6, int32 start @8, int32 stop @12,
    // hibernate_t hibernate[2] @16. UIUCTag/src/config.c readConfig().
    const auto it = src.Identity().regions.find("stored_config");
    if (it == src.Identity().regions.end())
      return Fail(error, "identity record has no stored_config region");
    uint8_t c[32];
    if (!src.Flash(it->second.start, c, sizeof c))
      return Fail(error, "stored configuration outside the captured image");
    config.Clear();
    config.set_tag_type(UIUCTAG);
    Adxl362 *adxl = config.mutable_adxl362();
    adxl->set_act_thresh_g(Le16(c) / 1000.0f);
    adxl->set_inact_thresh_g(kInactThreshMg / 1000.0f);
    adxl->set_inactive_sec(static_cast<float>(Le16(c + 4)));
    adxl->set_accel_type(Adxl362_AdxlType_AdxlType_367);
    Config_Interval *active = config.mutable_active_interval();
    active->set_start_epoch(static_cast<int32_t>(Le32(c + 8)));
    active->set_end_epoch(static_cast<int32_t>(Le32(c + 12)));
    for (int i = 0; i < 2; i++) {
      Config_Interval *h = config.add_hibernate();
      h->set_start_epoch(static_cast<int32_t>(Le32(c + 16 + 8 * i)));
      h->set_end_epoch(static_cast<int32_t>(Le32(c + 20 + 8 * i)));
    }
    return true;
  }

  uint64_t ExternalFlashSize(const CaptureSource &src) const override {
    return src.Identity().flash_size;
  }

  uint32_t DataLogCount(const CaptureSource &src) const override {
    return StatusCount(src);
  }

  void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const override {
    ack.Clear();
    ack.set_err(Ack_Err_NODATA);
    const IdentityRecord &id = src.Identity();
    if (index >= src.BackupWord(id.backup_state.word_pages))
      return;
    const auto dh = id.regions.find("data_headers");
    const auto ps = id.regions.find("persistent");
    if (dh == id.regions.end() || ps == id.regions.end())
      return;
    const uint32_t end = dh->second.end ? dh->second.end : ps->second.end;
    const uint64_t address = uint64_t(dh->second.start) + uint64_t(index) * kHeaderBytes;
    uint8_t h[kHeaderBytes];
    if (address + kHeaderBytes > end ||
        !src.Flash(static_cast<uint32_t>(address), h, sizeof h) || Le32(h) == kErased32)
      return;
    const uint64_t byte_offset = uint64_t(Le16(h + 6)) * kBlockBytes;
    const uint64_t flash_size = ExternalFlashSize(src);
    if (byte_offset >= flash_size)
      return;
    uint8_t block[kBlockBytes];
    std::memset(block, 0xFF, sizeof block);
    uint32_t count = kBlockBytes;
    if (byte_offset + count > flash_size)
      count = static_cast<uint32_t>(flash_size - byte_offset);
    if (!src.External(byte_offset, block, count))
      return;
    // Trim trailing never-written slots, by the firmware's own rule.
    size_t used = kBlockBytes;
    while (used >= kSlotBytes) {
      t_UIUCTagSample slot;
      std::memcpy(&slot, block + used - kSlotBytes, sizeof slot);
      if (!uiuctagSampleErased(&slot))
        break;
      used -= kSlotBytes;
    }
    UIUCTagLog *log = ack.mutable_uiuctag_data_log();
    log->set_epoch(static_cast<int32_t>(Le32(h)));
    log->set_voltage(Le16(h + 4) * 0.01f);
    log->set_samples(block, used);
    ack.set_err(Ack_Err_OK);
  }
};

/**
 * @brief   BitTag (decoder "bittag").
 *
 * @details Follows embedded/tags/BitTag/src/datalog.c and bt_config.c. There
 *          is no external flash: the log is the internal records
 *          vddHeader[] {int32 epoch, int16 temp10, uint16 vdd100, uint64
 *          activity}, served up to BitTagLog.data max_count (30) per Ack from
 *          the requested index until the first unwritten record. An empty Ack
 *          ends the download.
 *
 *          readConfig() depends on the tag's state: in IDLE or TEST it
 *          reports the nanopb default Config built into the image, which the
 *          identity record locates; otherwise the stored configuration.
 */
class BitTagDecoder : public CaptureDecoder {
public:
  static constexpr uint32_t kRecordBytes = 16;   ///< sizeof(t_DataHeader)
  static constexpr uint32_t kPerAck = 30;        ///< BitTagLog.data max_count

  std::vector<KnownLayout> Layouts() const override {
    return {{"stored_config", 1, 0}, {"stored_config", 2, 0},
            {"data_headers", 1, kRecordBytes}};
  }

  bool ReadConfig(const CaptureSource &src, Config &config,
                  std::string *error) const override {
    const IdentityRecord &id = src.Identity();
    config.Clear();
    const uint32_t state = src.BackupWord(id.backup_state.word_state);
    if (state == IDLE || state == TEST) {
      // readDefaultConfig(): the image's nanopb default, decoded as is.
      uint32_t len = 0;
      if (!id.has_default_config ||
          !src.Flash(id.default_config_len_addr, &len, sizeof len) || len > 4096)
        return Fail(error, "BitTag in IDLE or TEST reports its default config, "
                           "which the identity record does not locate");
      std::vector<uint8_t> blob(len);
      if (!src.Flash(id.default_config_addr, blob.data(), len) ||
          !config.ParseFromArray(blob.data(), static_cast<int>(len)))
        return Fail(error, "BitTag default config unreadable");
      return true;
    }
    // t_storedconfig (BitTag/inc/config.h): u16 adxl_act_thresh_cnt @0, u16
    // adxl_inact_thresh_cnt @2, u16 adxl_inactive_samples @4, u8
    // adxl_filter_range_rate @6, u8 internal_format @7, int32 start @12,
    // int32 stop @16, hibernate_t hibernate[2] @20.
    const auto it = id.regions.find("stored_config");
    if (it == id.regions.end())
      return Fail(error, "identity record has no stored_config region");
    uint8_t c[36];
    if (!src.Flash(it->second.start, c, sizeof c))
      return Fail(error, "stored configuration outside the captured image");
    // ADXL362 sensitivity per range, g per count (Sens[] in bt_config.c);
    // a range above 8 g reads as BITTAG_LE_RANGE (4 g).
    static const float kSens[] = {0.001f, 0.002f, 0.004f};
    int range = (c[6] >> 6) & 3;
    if (range > 2)
      range = 1;
    config.set_tag_type(BITTAG_LE);
    Adxl362 *adxl = config.mutable_adxl362();
    adxl->set_act_thresh_g(static_cast<int>(Le16(c)) * kSens[range]);
    adxl->set_inact_thresh_g(static_cast<int>(Le16(c + 2)) * kSens[range]);
    adxl->set_inactive_sec(static_cast<float>(static_cast<int>(Le16(c + 4))));
    Config_Interval *active = config.mutable_active_interval();
    active->set_start_epoch(static_cast<int32_t>(Le32(c + 12)));
    active->set_end_epoch(static_cast<int32_t>(Le32(c + 16)));
    config.set_bittag_log(static_cast<BitTagLogFmt>(c[7]));
    for (int i = 0; i < 2; i++) {
      Config_Interval *h = config.add_hibernate();
      h->set_start_epoch(static_cast<int32_t>(Le32(c + 20 + 8 * i)));
      h->set_end_epoch(static_cast<int32_t>(Le32(c + 24 + 8 * i)));
    }
    return true;
  }

  uint64_t ExternalFlashSize(const CaptureSource &) const override { return 0; }

  uint32_t DataLogCount(const CaptureSource &src) const override {
    return StatusCount(src);
  }

  void DataLog(const CaptureSource &src, uint32_t index, Ack &ack) const override {
    ack.Clear();
    ack.set_err(Ack_Err_OK);
    BitTagLog *log = ack.mutable_bittag_data_log();
    const IdentityRecord &id = src.Identity();
    const auto dh = id.regions.find("data_headers");
    const auto ps = id.regions.find("persistent");
    if (dh == id.regions.end() || ps == id.regions.end())
      return;
    const uint32_t end = dh->second.end ? dh->second.end : ps->second.end;
    for (uint32_t n = 0; n < kPerAck; n++, index++) {
      const uint64_t address = uint64_t(dh->second.start) + uint64_t(index) * kRecordBytes;
      uint8_t r[kRecordBytes];
      if (address + kRecordBytes > end ||
          !src.Flash(static_cast<uint32_t>(address), r, sizeof r) || Le32(r) == kErased32)
        break;
      BitTagData *d = log->add_data();
      d->set_epoch(static_cast<int32_t>(Le32(r)));
      d->set_temperature(static_cast<int16_t>(Le16(r + 4)) * 0.1f);
      d->set_voltage(Le16(r + 6) * 0.01f);
      d->set_rawdata(uint64_t(Le32(r + 8)) | (uint64_t(Le32(r + 12)) << 32));
    }
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

bool CaptureSource::External(uint64_t offset, void *out, uint32_t len) const {
  if (external_.empty())
    return false;
  uint8_t *o = static_cast<uint8_t *>(out);
  for (uint32_t i = 0; i < len; i++)
    o[i] = offset + i < external_.size() ? external_[offset + i] : 0xFF;
  return true;
}

std::string CaptureSource::ManifestString(const std::string &key) const {
  const auto it = manifest_strings_.find(key);
  return it == manifest_strings_.end() ? std::string() : it->second;
}

bool CaptureSource::Open(const std::string &dir, std::string *error) {
  dir_ = dir;
  std::string text;
  if (!ReadText(dir + "/manifest.json", text))
    return Fail(error, "no manifest.json in " + dir);
  google::protobuf::Struct manifest;
  if (!google::protobuf::util::JsonStringToMessage(text, &manifest).ok())
    return Fail(error, "manifest.json does not parse");
  for (const auto &kv : manifest.fields())
    if (kv.second.has_string_value())
      manifest_strings_[kv.first] = kv.second.string_value();

  // Every file the rebuild reads is checked against the hash tag-capture
  // recorded for it, and refused when the capture marked it failed: a
  // truncated or edited file would otherwise read as erased flash.
  struct Recorded {
    std::string sha256;
    bool ok = true;
  };
  std::map<std::string, Recorded> recorded;
  const auto &mf = manifest.fields();
  if (mf.count("regions"))
    for (const auto &v : mf.at("regions").list_value().values()) {
      const auto &f = v.struct_value().fields();
      if (!f.count("file") || !f.count("sha256"))
        continue;
      Recorded r;
      r.sha256 = f.at("sha256").string_value();
      r.ok = (!f.count("ok") || f.at("ok").bool_value()) &&
             (!f.count("failed") || f.at("failed").list_value().values_size() == 0);
      recorded[f.at("file").string_value()] = r;
    }
  bool external_ok = false;
  std::string external_error = "not captured";
  if (mf.count("external_flash")) {
    const auto &x = mf.at("external_flash").struct_value().fields();
    external_ok = x.count("captured") && x.at("captured").bool_value() &&
                  (!x.count("error") || x.at("error").string_value().empty());
    if (x.count("error") && !x.at("error").string_value().empty())
      external_error = x.at("error").string_value();
    if (x.count("files"))
      for (const auto &v : x.at("files").list_value().values()) {
        const auto &f = v.struct_value().fields();
        if (f.count("file") && f.count("sha256"))
          recorded[f.at("file").string_value()] = {f.at("sha256").string_value(), external_ok};
      }
  }
  // Load @p file, checking it; a missing optional file is not an error.
  auto load = [&](const std::string &file, std::vector<uint8_t> &out, bool required,
                  bool &present) -> bool {
    present = false;
    out.clear();
    const auto it = recorded.find(file);
    if (!ReadFile(dir + "/" + file, out)) {
      if (required || it != recorded.end())
        return Fail(error, "no " + file + " in " + dir);
      return true;
    }
    if (it == recorded.end())
      return Fail(error, file + " is not recorded in manifest.json");
    if (!it->second.ok)
      return Fail(error, file + " is marked failed in manifest.json (" +
                             (file.rfind("external_", 0) == 0 ? external_error
                                                              : std::string("read errors")) +
                             ")");
    if (Sha256::Of(out.data(), out.size()) != it->second.sha256)
      return Fail(error, file + " does not match its SHA-256 in manifest.json");
    present = true;
    return true;
  };
  bool present = false;
  if (!load("internal_flash.bin", flash_, true, present) ||
      !load("backup_regs.bin", backup_, true, present))
    return false;
  uint32_t uid[3] = {0, 0, 0};
  bool have_uid[3] = {false, false, false};
  const auto regs = manifest.fields().find("registers");
  if (regs != manifest.fields().end())
    for (const auto &v : regs->second.list_value().values()) {
      const auto &f = v.struct_value().fields();
      const std::string name = f.count("name") ? f.at("name").string_value() : "";
      uint32_t value = 0;
      if (!f.count("value") || !ParseU32(f.at("value").string_value(), value))
        continue;
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
  if (!load("external_ecc.bin", ecc_, false, present))
    return false;
  if (present) {
    std::vector<uint8_t> csv_bytes;
    bool csv_present = false;
    if (!load("external_pages.csv", csv_bytes, true, csv_present))
      return false;
    const std::string csv(csv_bytes.begin(), csv_bytes.end());
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
      uint32_t page_index = 0;
      if (!ParseU32(page, page_index))
        return Fail(error, "external_pages.csv: bad page number \"" + page + "\"");
      ecc_index_[page_index] = {row++, verdict};
    }
    if (stored_page == 0 || ecc_.size() != row * stored_page)
      return Fail(error, "external_ecc.bin holds " + std::to_string(ecc_.size()) +
                             " bytes, not the " + std::to_string(row) +
                             " pages external_pages.csv lists");
    // ecc_ holds whole stored pages (data + spare); keep the data areas only.
    std::vector<uint8_t> data;
    data.reserve(row * page_bytes_);
    for (size_t r = 0; r < row && (r + 1) * stored_page <= ecc_.size(); r++)
      data.insert(data.end(), ecc_.begin() + r * stored_page,
                  ecc_.begin() + r * stored_page + page_bytes_);
    ecc_.swap(data);
  }

  if (!load("external_flash.bin", external_, false, present)) // linear parts (NOR)
    return false;
  if (present && identity_.has_external_flash && external_.size() != identity_.flash_size)
    return Fail(error, "external_flash.bin holds " + std::to_string(external_.size()) +
                           " bytes; the identity record says the part has " +
                           std::to_string(identity_.flash_size));

  const std::string decoder = identity_.String("decoder");
  if (decoder == "imutag" && page_bytes_ != 0)
    decoder_ = std::make_unique<ImuTagNandDecoder>();
  else if (decoder == "prestag" && !external_.empty())
    decoder_ = std::make_unique<PresTagDecoder>();
  else if (decoder == "compasstag" && !external_.empty())
    decoder_ = std::make_unique<CompassTagDecoder>();
  else if (decoder == "uiuctag" && !external_.empty())
    decoder_ = std::make_unique<UiucTagDecoder>();
  else if (decoder == "bittag")
    decoder_ = std::make_unique<BitTagDecoder>();
  else
    return Fail(error, "capture rebuild not implemented for decoder \"" + decoder +
                           "\", or its external flash was not captured");
  // The layouts read in common code, then the family's own.
  const std::vector<KnownLayout> common = {
      {"state_log", 1, 24}, {"state_log", 1, 32}, {"calibration", 1, 0}};
  if (!CheckLayouts(identity_, common, error) ||
      !CheckLayouts(identity_, decoder_->Layouts(), error)) {
    decoder_.reset();
    return false;
  }
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
    for (uint32_t i = 0; i < RegionRecords(id, cal->second); i++) {
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
    for (uint32_t i = 0; i < RegionRecords(id, sl->second); i++) {
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

namespace {

/** @brief Write the rebuild into @p db_path; the caller cleans up on failure. */
bool WriteRebuild(const CaptureSource &src, const TagLogHeader &header,
                  const std::string &capture_dir, const std::string &db_path,
                  uint32_t *records, std::string *error) {
  SqliteTagLogWriter writer(db_path, header.config, true);
  if (!writer.isOpen())
    return Fail(error, writer.lastError());
  if (!writer.writeHeader(header) || !writer.writeInfo("source", "capture") ||
      !writer.writeInfo("capture_dir", capture_dir) ||
      !writer.writeInfo("captured_at", src.ManifestString("captured_at")))
    return Fail(error, writer.lastError());
  // The download loop of host/commandline/dwnld.cc, which writes the header
  // and stops when the Status count is zero ("No log records to download").
  const uint32_t max_count = src.DataLogCount();
  if (max_count == 0)
    return true;
  if (!writer.beginLog())
    return Fail(error, writer.lastError());

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

} // namespace

bool RebuildSqliteFromCapture(const std::string &capture_dir,
                              const std::string &db_path, uint32_t *records,
                              std::string *error) {
  CaptureSource src;
  if (!src.Open(capture_dir, error))
    return false;
  TagLogHeader header;
  if (!src.Header(header, error))
    return false;
  // Nothing is written until the capture has been checked; past that point a
  // failure removes the partial file, so it cannot pass for a rebuild.
  if (!WriteRebuild(src, header, capture_dir, db_path, records, error)) {
    std::remove(db_path.c_str());
    return false;
  }
  return true;
}

} // namespace tagcore::recovery
