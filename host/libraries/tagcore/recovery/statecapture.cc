/**
 * @file    statecapture.cc
 * @brief   Register, internal-flash and SRAM capture; see statecapture.h.
 */

#include "recovery/statecapture.h"

#include "recovery/sha256.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

extern "C" {
#include "log.h"
}

namespace tagcore::recovery {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kMonEn = 1U << 16;
constexpr uint32_t kVcCoreReset = 1U << 0;

/// Upper bound on a believable flash-size register value, in KB.
constexpr uint32_t kMaxFlashKb = 4096;

std::string Hex32(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08X", v);
  return buf;
}

std::string UtcStamp(const char *format) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN64
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), format, &tm);
  return buf;
}

std::string JsonString(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char esc[8];
        std::snprintf(esc, sizeof(esc), "\\u%04x", c);
        out += esc;
      } else {
        out += c;
      }
    }
  }
  return out + "\"";
}

void Progress(const CaptureOptions &o, const std::string &line) {
  if (o.progress)
    o.progress(line);
}

bool WriteFile(const fs::path &path, const std::vector<uint8_t> &data) {
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

NamedRegister ReadNamed(SwdSession &s, const char *name, uint32_t addr) {
  NamedRegister r;
  r.name = name;
  r.addr = addr;
  r.ok = s.ReadWord(addr, r.value);
  return r;
}

/// Read one region into a file and record it.
CapturedRegion CaptureRegion(SwdSession &s, const fs::path &dir,
                             const CaptureOptions &o, const std::string &name,
                             uint32_t addr, uint32_t size, uint32_t chunk) {
  CapturedRegion r;
  r.name = name;
  r.file = name + ".bin";
  r.addr = addr;
  r.size = size;

  Progress(o, "reading " + name + " (" + std::to_string(size) + " bytes at " +
                  Hex32(addr) + ")");
  std::vector<uint8_t> data(size, 0);
  const auto start = Clock::now();
  r.ok = s.Read(addr, data.data(), size, chunk, &r.failed);
  r.seconds = std::chrono::duration<double>(Clock::now() - start).count();

  if (!WriteFile(dir / r.file, data)) {
    log_error("could not write %s", (dir / r.file).string().c_str());
    r.ok = false;
  }
  r.sha256 = Sha256::Of(data.data(), data.size());
  return r;
}

const NamedRegister *FindRegister(const CaptureResult &r, const std::string &name) {
  for (const NamedRegister &reg : r.registers)
    if (reg.name == name && reg.ok)
      return &reg;
  return nullptr;
}

bool IsL4(const McuMap *m) { return m && m->dev_id == 0x435; }

/// Option-byte fields that change what the capture means.
std::string DecodeOptions(const McuMap *m, uint32_t optr) {
  const uint32_t rdp = optr & 0xFFU;
  const bool iwdg_sw = (optr >> 16) & 1U;
  const bool sram2_kept = (optr >> 25) & 1U;
  std::ostringstream j;
  j << "{\"flash_optr\": " << JsonString(Hex32(optr))
    << ", \"rdp\": " << JsonString(Hex32(rdp))
    << ", \"rdp_level0\": " << (rdp == 0xAA ? "true" : "false")
    << ", \"iwdg_sw\": " << iwdg_sw
    << ", \"sram2_rst\": " << sram2_kept;
  std::vector<std::string> erased;
  if (!sram2_kept)
    erased.push_back("sram2");
  if (!IsL4(m)) {
    // U3 bit 15 is SRAM_RST or SRAM1_RST depending on the part variant; both
    // cover SRAM1.
    const bool sram1_kept = (optr >> 15) & 1U;
    j << ", \"sram1_rst\": " << sram1_kept;
    if (!sram1_kept)
      erased.insert(erased.begin(), "sram1");
  }
  j << ", \"sram_erased_by_attach\": [";
  for (size_t i = 0; i < erased.size(); i++)
    j << (i ? ", " : "") << JsonString(erased[i]);
  j << "]}";
  return j.str();
}

/// RCC_CSR reset flags. Decoded for STM32L4 only; raw for other parts.
std::string DecodeResetFlags(const McuMap *m, uint32_t csr) {
  std::ostringstream j;
  j << "{\"rcc_csr\": " << JsonString(Hex32(csr)) << ", \"flags\": [";
  if (IsL4(m)) {
    static const char *kNames[8] = {"FWRSTF",  "OBLRSTF",  "PINRSTF",
                                    "BORRSTF", "SFTRSTF",  "IWDGRSTF",
                                    "WWDGRSTF", "LPWRRSTF"};
    bool first = true;
    for (int bit = 24; bit < 32; bit++)
      if (csr & (1U << bit)) {
        j << (first ? "" : ", ") << JsonString(kNames[bit - 24]);
        first = false;
      }
  }
  j << "], \"note\": \"PINRSTF includes the capture's own attach\"}";
  return j.str();
}

std::string BuildManifest(const CaptureOptions &o, const CaptureResult &r,
                          const McuMap *m, uint32_t flash_kb,
                          const std::vector<std::string> &clock_notes) {
  const SwdAttachInfo &a = r.attach;
  std::ostringstream j;
  j << "{\n";
  j << "  \"format\": \"tag-capture\",\n  \"format_version\": 1,\n";
  j << "  \"captured_at\": " << JsonString(UtcStamp("%Y-%m-%dT%H:%M:%SZ")) << ",\n";
  j << "  \"reason\": " << JsonString(o.reason) << ",\n";
  j << "  \"capture_order\": [\"registers\", \"info_regions\", "
       "\"internal_flash\", \"ecc_after\", \"sram\"],\n";
  j << "  \"external_flash\": \"not captured (needs the loader)\",\n";
  j << "  \"target\": {\"mcu\": " << JsonString(r.mcu)
    << ", \"idcode\": " << JsonString(Hex32(a.idcode))
    << ", \"uid\": " << JsonString(r.uid) << ", \"flash_kb\": " << flash_kb;
  char volts[16];
  std::snprintf(volts, sizeof(volts), "%.2f", a.voltage);
  j << ", \"voltage\": " << volts << "},\n";

  j << "  \"attach\": {\"dhcsr_before\": " << JsonString(Hex32(a.dhcsr_before))
    << ", \"demcr_before\": " << JsonString(Hex32(a.demcr_before))
    << ", \"monitor_left_attached\": "
    << ((a.demcr_before & (kMonEn | kVcCoreReset)) ? "true" : "false")
    << ", \"dbgmcu_apb1fzr1_before\": " << JsonString(Hex32(a.fz_before))
    << ", \"halted\": " << (a.halted ? "true" : "false")
    << ", \"pc\": " << JsonString(Hex32(a.pc))
    << ", \"reset_vector\": " << JsonString(Hex32(a.reset_vector))
    << ", \"at_reset_vector\": " << (a.at_reset_vector ? "true" : "false")
    << ", \"flash_blank\": " << (a.flash_blank ? "true" : "false") << "},\n";

  if (const NamedRegister *optr = FindRegister(r, "FLASH_OPTR"))
    j << "  \"option_bytes\": " << DecodeOptions(m, optr->value) << ",\n";
  if (const NamedRegister *csr = FindRegister(r, "RCC_CSR"))
    j << "  \"reset_flags\": " << DecodeResetFlags(m, csr->value) << ",\n";

  j << "  \"clock_enables_set\": [";
  for (size_t i = 0; i < clock_notes.size(); i++)
    j << (i ? ", " : "") << JsonString(clock_notes[i]);
  j << "],\n";

  j << "  \"registers\": [\n";
  for (size_t i = 0; i < r.registers.size(); i++) {
    const NamedRegister &reg = r.registers[i];
    j << "    {\"name\": " << JsonString(reg.name)
      << ", \"addr\": " << JsonString(Hex32(reg.addr))
      << ", \"value\": " << JsonString(Hex32(reg.value))
      << ", \"ok\": " << (reg.ok ? "true" : "false") << "}"
      << (i + 1 < r.registers.size() ? ",\n" : "\n");
  }
  j << "  ],\n";

  j << "  \"regions\": [\n";
  for (size_t i = 0; i < r.regions.size(); i++) {
    const CapturedRegion &g = r.regions[i];
    char rate[32];
    std::snprintf(rate, sizeof(rate), "%.0f",
                  g.seconds > 0 ? g.size / g.seconds : 0.0);
    char secs[32];
    std::snprintf(secs, sizeof(secs), "%.3f", g.seconds);
    j << "    {\"name\": " << JsonString(g.name)
      << ", \"file\": " << JsonString(g.file)
      << ", \"addr\": " << JsonString(Hex32(g.addr))
      << ", \"size\": " << g.size << ", \"ok\": " << (g.ok ? "true" : "false")
      << ", \"sha256\": " << JsonString(g.sha256) << ", \"seconds\": " << secs
      << ", \"bytes_per_second\": " << rate << ", \"failed\": [";
    for (size_t k = 0; k < g.failed.size(); k++)
      j << (k ? ", " : "") << "[" << JsonString(Hex32(g.failed[k].first))
        << ", " << g.failed[k].second << "]";
    j << "]}" << (i + 1 < r.regions.size() ? ",\n" : "\n");
  }
  j << "  ],\n";

  j << "  \"complete\": " << (r.complete ? "true" : "false") << ",\n";
  j << "  \"exit\": "
    << JsonString(o.exit == SwdExit::HardwareReset ? "hardware_reset"
                                                   : "leave_halted")
    << "\n}\n";
  return j.str();
}

} // namespace

bool CaptureState(SwdSession &s, const CaptureOptions &o, CaptureResult &r) {
  r = CaptureResult();
  if (!s.IsOpen() || !s.Mcu()) {
    r.error = "session not open";
    return false;
  }
  const McuMap *m = s.Mcu();
  r.mcu = m->name;
  r.attach = s.AttachInfo();

  const fs::path dir =
      fs::path(o.parent_dir) / ("capture-" + UtcStamp("%Y%m%d-%H%M%S"));
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    r.error = "could not create " + dir.string() + ": " + ec.message();
    return false;
  }
  r.dir = dir.string();

  // 1. Registers. Option bytes and ECC first: the ECC registers may hold the
  //    tag's own last fault, and reading flash can overwrite them.
  Progress(o, "reading registers");
  r.registers.push_back(ReadNamed(s, "FLASH_OPTR", m->flash_optr));
  for (size_t i = 0; i < m->ecc_regs.size(); i++)
    r.registers.push_back(ReadNamed(
        s, ("FLASH_ECC" + std::to_string(i) + "_before").c_str(), m->ecc_regs[i]));
  r.registers.push_back(ReadNamed(s, "RCC_CSR", m->rcc_csr));
  r.registers.push_back(ReadNamed(s, "RCC_BDCR", m->rcc_bdcr));
  r.registers.push_back(ReadNamed(s, "UID0", m->uid));
  r.registers.push_back(ReadNamed(s, "UID1", m->uid + 4));
  r.registers.push_back(ReadNamed(s, "UID2", m->uid + 8));
  r.registers.push_back(ReadNamed(s, "FLASH_SIZE", m->flashsize_reg));

  // The raw RCC block is read before any clock enable is changed.
  for (const McuRegion &blk : m->register_blocks)
    if (std::string(blk.name) == "rcc")
      r.regions.push_back(CaptureRegion(s, dir, o, blk.name, blk.addr, blk.size, 512));

  // Backup registers and RTC need their APB clock; on L4 PWR does too. The
  // attach reset RCC, so these enables only undo that reset, and the tag's own
  // boot resets them again.
  std::vector<std::string> clock_notes;
  uint32_t enr = 0;
  if (s.ReadWord(m->rcc_apb1enr1, enr)) {
    const uint32_t want = enr | m->rtcapben | m->pwren;
    if (want != enr && s.WriteWord(m->rcc_apb1enr1, want))
      clock_notes.push_back("RCC_APB1ENR1 " + Hex32(enr) + " -> " + Hex32(want));
  }

  r.registers.push_back(ReadNamed(s, "RTC_TR", m->rtc_tr));
  r.registers.push_back(ReadNamed(s, "RTC_DR", m->rtc_dr));
  for (const McuRegion &blk : m->register_blocks)
    if (std::string(blk.name) != "rcc")
      r.regions.push_back(CaptureRegion(s, dir, o, blk.name, blk.addr, blk.size, 512));
  r.regions.push_back(CaptureRegion(s, dir, o, "backup_regs", m->backup_regs,
                                    m->backup_count * 4, 512));
  for (const McuRegion &info : m->info_regions)
    r.regions.push_back(CaptureRegion(s, dir, o, info.name, info.addr, info.size, 512));

  if (const NamedRegister *u0 = FindRegister(r, "UID0")) {
    const NamedRegister *u1 = FindRegister(r, "UID1");
    const NamedRegister *u2 = FindRegister(r, "UID2");
    if (u1 && u2) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%08X%08X%08X", u2->value, u1->value, u0->value);
      r.uid = buf; // same form as the monitor's info.uuid
    }
  }

  // 2. Internal flash, sized by the part's own flash-size register.
  uint32_t flash_kb = 0;
  if (const NamedRegister *fsz = FindRegister(r, "FLASH_SIZE"))
    flash_kb = fsz->value & 0xFFFFU;
  if (flash_kb == 0 || flash_kb > kMaxFlashKb) {
    log_error("implausible flash size register %u KB; internal flash skipped", flash_kb);
  } else {
    r.regions.push_back(CaptureRegion(s, dir, o, "internal_flash", m->flash_base,
                                      flash_kb * 1024, m->flash_page));
  }
  for (size_t i = 0; i < m->ecc_regs.size(); i++)
    r.registers.push_back(ReadNamed(
        s, ("FLASH_ECC" + std::to_string(i) + "_after").c_str(), m->ecc_regs[i]));

  // 3. SRAM.
  if (o.include_sram)
    for (const McuRegion &sram : m->sram)
      r.regions.push_back(CaptureRegion(s, dir, o, sram.name, sram.addr, sram.size, 4096));

  r.complete = flash_kb != 0;
  for (const NamedRegister &reg : r.registers)
    r.complete = r.complete && reg.ok;
  for (const CapturedRegion &g : r.regions)
    r.complete = r.complete && g.ok;

  const fs::path manifest = dir / "manifest.json";
  std::ofstream f(manifest);
  f << BuildManifest(o, r, m, flash_kb, clock_notes);
  if (!f) {
    r.error = "could not write " + manifest.string();
    return false;
  }
  r.manifest = manifest.string();
  return true;
}

bool CaptureTag(const CaptureOptions &o, CaptureResult &r, UsbDev usbdev) {
  SwdSession session;
  if (!session.Open(usbdev)) {
    r = CaptureResult();
    r.attach = session.AttachInfo();
    r.error = "could not open an SWD session halted at reset";
    return false;
  }
  const bool ok = CaptureState(session, o, r);
  session.Close(o.exit);
  return ok;
}

} // namespace tagcore::recovery
