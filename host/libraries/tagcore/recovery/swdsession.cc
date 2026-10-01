/**
 * @file    swdsession.cc
 * @brief   Attach-without-boot SWD session; see swdsession.h.
 */

#include "recovery/swdsession.h"

#include <chrono>
#include <cstring>
#include <thread>

extern "C" {
#include "log.h"
}

namespace tagcore::recovery {

namespace {

// ARMv7-M debug registers.
constexpr uint32_t kDhcsr = 0xE000EDF0U;
constexpr uint32_t kDcrsr = 0xE000EDF4U;
constexpr uint32_t kDcrdr = 0xE000EDF8U;
constexpr uint32_t kDemcr = 0xE000EDFCU;

constexpr uint32_t kDbgKey = 0xA05FU << 16;
constexpr uint32_t kCDebugEn = 1U << 0;
constexpr uint32_t kCHalt = 1U << 1;
constexpr uint32_t kCMaskInts = 1U << 3;
constexpr uint32_t kRegWnR = 1U << 16;
constexpr uint32_t kSRegRdy = 1U << 16;
constexpr uint32_t kSHalt = 1U << 17;
constexpr uint32_t kVcCoreReset = 1U << 0;

/// The base streams reads in 512-byte pieces and reports faults per request.
constexpr uint32_t kBasePiece = 512;

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool PlausibleIdcode(uint32_t idcode) {
  return idcode != 0 && idcode != 0xFFFFFFFFU;
}

} // namespace

SwdSession::~SwdSession() { Close(SwdExit::HardwareReset); }

bool SwdSession::ReadWord(uint32_t addr, uint32_t &value) {
  return ReadDebug32(addr, &value);
}

bool SwdSession::WriteWord(uint32_t addr, uint32_t value) {
  return WriteDebug32(addr, value);
}

bool SwdSession::ReadCoreRegister(uint32_t reg, uint32_t &value) {
  if (!WriteDebug32(kDcrsr, reg))
    return false;
  for (int i = 0; i < 100; i++) {
    uint32_t dhcsr = 0;
    if (ReadDebug32(kDhcsr, &dhcsr) && (dhcsr & kSRegRdy))
      return ReadDebug32(kDcrdr, &value);
    SleepMs(1);
  }
  return false;
}

bool SwdSession::WriteCoreRegister(uint32_t reg, uint32_t value) {
  if (!WriteDebug32(kDcrdr, value) || !WriteDebug32(kDcrsr, reg | kRegWnR))
    return false;
  for (int i = 0; i < 100; i++) {
    uint32_t dhcsr = 0;
    if (ReadDebug32(kDhcsr, &dhcsr) && (dhcsr & kSRegRdy))
      return true;
    SleepMs(1);
  }
  return false;
}

bool SwdSession::Write(uint32_t addr, const uint8_t *buf, uint32_t len) {
  for (uint32_t off = 0; off < len; off += kBasePiece) {
    const uint32_t n = (len - off) < kBasePiece ? (len - off) : kBasePiece;
    // WriteMem32 takes a non-const buffer but only reads it.
    if (!WriteMem32(addr + off, const_cast<uint8_t *>(buf + off),
                    static_cast<uint16_t>(n)))
      return false;
  }
  return true;
}

bool SwdSession::Run() {
  // C_MASKINTS may only change while halted: set it with C_HALT still set,
  // then release the halt with it held.
  return WriteDebug32(kDhcsr, kDbgKey | kCDebugEn | kCHalt | kCMaskInts) &&
         WriteDebug32(kDhcsr, kDbgKey | kCDebugEn | kCMaskInts);
}

bool SwdSession::WaitHalt(int timeout_ms, uint32_t *dhcsr_out) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  uint32_t dhcsr = 0;
  for (;;) {
    if (ReadDebug32(kDhcsr, &dhcsr) && (dhcsr & kSHalt)) {
      if (dhcsr_out)
        *dhcsr_out = dhcsr;
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      break;
    SleepMs(1);
  }
  if (dhcsr_out)
    *dhcsr_out = dhcsr;
  return false;
}

bool SwdSession::Halt() {
  return WriteDebug32(kDhcsr, kDbgKey | kCDebugEn | kCHalt | kCMaskInts) &&
         WaitHalt(100);
}

bool SwdSession::Open(UsbDev usbdev) {
  if (open_) {
    log_error("SWD session already open");
    return false;
  }
  info_ = SwdAttachInfo();
  mcu_ = nullptr;

  // NRST asserted, SWD entered. The claim fails if another session or
  // application holds the base.
  if (!LinkAdapt::Attach(true, usbdev)) {
    log_error("could not attach: no base, or the base is in use by another "
              "session or application");
    return false;
  }
  SleepMs(20);

  for (const McuMap *m : AllMcuMaps()) {
    uint32_t idcode = 0;
    if (ReadDebug32(m->dbgmcu_idcode, &idcode) && PlausibleIdcode(idcode) &&
        FindMcuMap(idcode) == m) {
      mcu_ = m;
      info_.idcode = idcode;
      break;
    }
  }
  if (!mcu_) {
    log_error("unsupported or unreadable MCU");
    AssertReset(true);
    LinkAdapt::Detach();
    return false;
  }

  Voltage(info_.voltage);
  ReadDebug32(kDhcsr, &info_.dhcsr_before);
  ReadDebug32(kDemcr, &info_.demcr_before);
  ReadDebug32(mcu_->dbgmcu_apb1fzr1, &info_.fz_before);

  // Arm the halt, then let the core out of reset into the vector catch.
  // Monitor bits are deliberately not set: the firmware must not see a monitor.
  if (!WriteDebug32(kDhcsr, kDbgKey | kCDebugEn | kCHalt) ||
      !WriteDebug32(kDemcr, kVcCoreReset) || !AssertReset(true)) {
    log_error("could not arm the reset halt");
    open_ = true;
    Close(SwdExit::HardwareReset);
    return false;
  }

  for (int i = 0; i < 200; i++) {
    if (ReadDebug32(kDhcsr, &info_.dhcsr_halted) && (info_.dhcsr_halted & kSHalt)) {
      info_.halted = true;
      break;
    }
    SleepMs(1);
  }

  if (info_.halted) {
    ReadCoreRegister(15, info_.pc);
    ReadWord(mcu_->flash_base + 4, info_.reset_vector);
    info_.flash_blank = info_.reset_vector == 0xFFFFFFFFU;
    info_.at_reset_vector = info_.pc == (info_.reset_vector & ~1U);
  }

  open_ = true;
  if (!info_.halted || !(info_.at_reset_vector || info_.flash_blank)) {
    log_error("core did not halt at its reset vector (halted=%d pc=0x%08x "
              "vector=0x%08x): the firmware may have run",
              info_.halted, info_.pc, info_.reset_vector);
    Close(SwdExit::HardwareReset);
    return false;
  }

  WriteDebug32(mcu_->dbgmcu_apb1fzr1, info_.fz_before | mcu_->iwdg_freeze);
  return true;
}

void SwdSession::Close(SwdExit exit) {
  if (!open_)
    return;
  if (mcu_)
    WriteDebug32(mcu_->dbgmcu_apb1fzr1, info_.fz_before);

  if (exit == SwdExit::HardwareReset) {
    // Hold the core in reset while debug is disabled, so it comes out of
    // reset with no vector catch and no halt request, as after a plain
    // connection.
    AssertReset(false);
    SleepMs(5);
    WriteDebug32(kDemcr, 0);
    WriteDebug32(kDhcsr, kDbgKey);
    AssertReset(true);
    SleepMs(5);
  } else {
    WriteDebug32(kDemcr, 0);
  }

  LinkAdapt::Detach();
  open_ = false;
}

bool SwdSession::Read(uint32_t addr, uint8_t *buf, uint32_t len, uint32_t chunk,
                      std::vector<AddressRange> *failed) {
  bool all_ok = true;
  if (chunk == 0 || chunk % kBasePiece != 0 || chunk > 32768)
    chunk = 4096;

  for (uint32_t off = 0; off < len; off += chunk) {
    const uint32_t n = (len - off) < chunk ? (len - off) : chunk;
    if (ReadMem32(addr + off, buf + off, static_cast<uint16_t>(n)))
      continue;

    // Pin the fault down to the base's transfer unit.
    for (uint32_t p = 0; p < n; p += kBasePiece) {
      const uint32_t m = (n - p) < kBasePiece ? (n - p) : kBasePiece;
      if (!ReadMem32(addr + off + p, buf + off + p, static_cast<uint16_t>(m))) {
        std::memset(buf + off + p, 0, m);
        all_ok = false;
        if (failed)
          failed->push_back({addr + off + p, m});
      }
    }
  }
  return all_ok;
}

} // namespace tagcore::recovery
