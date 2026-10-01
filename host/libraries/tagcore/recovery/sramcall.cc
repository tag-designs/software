/**
 * @file    sramcall.cc
 * @brief   SRAM download and ST-style call; contract in sramcall.h.
 */

#include "recovery/sramcall.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace tagcore::recovery {

namespace {

/// Stack reserved past the image; CubeProgrammer gives about 1 KB.
constexpr uint32_t kStackBytes = 1024;
/// Largest transfer buffer: the session's largest single read.
constexpr uint32_t kMaxBuffer = 32768;
/// Core register selectors (DCRSR.REGSEL).
constexpr uint32_t kRegSp = 13, kRegLr = 14, kRegPc = 15, kRegXpsr = 16,
                   kRegMsp = 17;
/// xPSR with only the Thumb bit set.
constexpr uint32_t kXpsrThumb = 1U << 24;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

bool Fail(std::string *error, const std::string &why) {
  if (error)
    *error = why;
  return false;
}

std::string Hex(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%08X", v);
  return b;
}

} // namespace

bool SramCall::Download(const TargetImage &image, std::string *error) {
  image_ = nullptr;
  const McuMap *mcu = s_.Mcu();
  if (!s_.IsOpen() || !mcu || mcu->sram.empty())
    return Fail(error, "session not open");
  const uint32_t sram = mcu->sram[0].addr;
  const uint32_t sram_end = sram + mcu->sram[0].size;

  uint32_t image_end = sram + 4; // the trap word
  for (const ImageSegment &seg : image.Segments()) {
    if (seg.addr < sram || seg.addr >= sram_end)
      continue; // not for SRAM, e.g. a loader's descriptor at 0
    if (seg.addr < sram + 4 || uint64_t(seg.addr) + seg.memsz > sram_end)
      return Fail(error, "segment at " + Hex(seg.addr) + " overlaps the trap "
                         "or runs past " + Hex(sram_end));
    if (seg.addr % 4 != 0)
      return Fail(error, "segment at " + Hex(seg.addr) + " is not word aligned");
    std::vector<uint8_t> bytes(AlignUp(seg.memsz, 4), 0);
    std::copy(seg.data.begin(), seg.data.end(), bytes.begin());
    std::vector<uint8_t> back(bytes.size());
    if (!s_.Write(seg.addr, bytes.data(), bytes.size()) ||
        !s_.Read(seg.addr, back.data(), back.size(), 4096, nullptr) ||
        back != bytes)
      return Fail(error, "segment at " + Hex(seg.addr) + " did not write back");
    image_end = std::max(image_end, seg.addr + seg.memsz);
  }
  if (image_end == sram + 4)
    return Fail(error, image.Path() + " has nothing to load into SRAM");

  // BKPT #0 in both halfwords of the trap word.
  if (!s_.WriteWord(sram, 0xBE00BE00U))
    return Fail(error, "could not write the return trap");

  trap_ = sram;
  stack_top_ = AlignUp(image_end + kStackBytes, 8);
  buffer_ = AlignUp(stack_top_ + 64, 512);
  buffer_size_ = buffer_ < sram_end
                     ? std::min(kMaxBuffer, (sram_end - buffer_) & ~511U)
                     : 0;
  if (buffer_size_ < 512)
    return Fail(error, "no room for a transfer buffer");
  image_ = &image;
  return true;
}

bool SramCall::Start(const std::string &symbol,
                     std::initializer_list<uint32_t> args, std::string *error) {
  if (!image_)
    return Fail(error, "nothing downloaded");
  uint32_t entry = 0;
  if (!image_->Symbol(symbol, entry))
    return Fail(error, "no symbol " + symbol + " in " + image_->Path());
  if (args.size() > 4)
    return Fail(error, "at most four arguments");

  uint32_t r[13] = {};
  size_t i = 0;
  for (uint32_t a : args)
    r[i++] = a;
  for (uint32_t reg = 0; reg < 13; reg++)
    if (!s_.WriteCoreRegister(reg, r[reg]))
      return Fail(error, "could not set R" + std::to_string(reg));
  if (!s_.WriteCoreRegister(kRegMsp, stack_top_) ||
      !s_.WriteCoreRegister(kRegSp, stack_top_) ||
      !s_.WriteCoreRegister(kRegLr, trap_ | 1U) ||
      !s_.WriteCoreRegister(kRegPc, entry & ~1U) ||
      !s_.WriteCoreRegister(kRegXpsr, kXpsrThumb))
    return Fail(error, "could not set up the call frame");

  if (!s_.Run())
    return Fail(error, "could not start the core");
  running_ = symbol;
  return true;
}

bool SramCall::WaitReturn(uint32_t &result, int timeout_ms, std::string *error) {
  uint32_t dhcsr = 0;
  if (!s_.WaitHalt(timeout_ms, &dhcsr)) {
    s_.Halt();
    uint32_t pc = 0;
    s_.ReadCoreRegister(kRegPc, pc);
    return Fail(error, running_ + " did not return within " +
                           std::to_string(timeout_ms) + " ms (halted at pc " +
                           Hex(pc) + ")");
  }

  uint32_t pc = 0;
  if (!s_.ReadCoreRegister(kRegPc, pc) || !s_.ReadCoreRegister(0, result))
    return Fail(error, "could not read the result");
  if ((pc & ~1U) != trap_)
    return Fail(error, running_ + " halted at " + Hex(pc) +
                           ", not the return trap");
  return true;
}

bool SramCall::Call(const std::string &symbol,
                    std::initializer_list<uint32_t> args, uint32_t &result,
                    int timeout_ms, std::string *error) {
  return Start(symbol, args, error) && WaitReturn(result, timeout_ms, error);
}

} // namespace tagcore::recovery
