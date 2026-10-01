/**
 * @file    externalflash.cc
 * @brief   Serve() session; contract in externalflash.h.
 */

#include "recovery/externalflash.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" {
#include "loader_service.h"
}

namespace tagcore::recovery {

namespace {

/// Longest wait for Serve() to initialise: clock, board, SPI, flash wake.
constexpr int kReadyTimeoutMs = 2000;
/// Per-command budgets. An erase is bounded by the part driver's own budget.
constexpr int kReadTimeoutMs = 5000;
constexpr int kEraseTimeoutMs = 10000;
constexpr int kProgramTimeoutMs = 10000;

constexpr uint32_t kMagic = offsetof(LoaderServiceBlock, magic);
constexpr uint32_t kVersion = offsetof(LoaderServiceBlock, version);
constexpr uint32_t kSeq = offsetof(LoaderServiceBlock, seq);
constexpr uint32_t kAck = offsetof(LoaderServiceBlock, ack);
constexpr uint32_t kCmd = offsetof(LoaderServiceBlock, cmd);
constexpr uint32_t kOffset = offsetof(LoaderServiceBlock, offset);
constexpr uint32_t kLength = offsetof(LoaderServiceBlock, length);
constexpr uint32_t kStatus = offsetof(LoaderServiceBlock, status);
constexpr uint32_t kDetail = offsetof(LoaderServiceBlock, detail);

bool Fail(std::string *error, const std::string &why) {
  if (error)
    *error = why;
  return false;
}

const char *StatusName(int32_t st) {
  switch (st) {
  case LOADER_STATUS_OK: return "ok";
  case LOADER_STATUS_BAD_COMMAND: return "unknown command";
  case LOADER_STATUS_RANGE: return "out of range";
  case LOADER_STATUS_IO: return "SPI timeout or no response";
  case LOADER_STATUS_READ_ONLY: return "read-only loader";
  case LOADER_STATUS_VERIFY: return "did not read back";
  case LOADER_STATUS_INIT: return "loader initialisation failed";
  default: return "unknown status";
  }
}

} // namespace

ExternalFlash::~ExternalFlash() {
  if (open_)
    Close();
}

bool ExternalFlash::Field(uint32_t off, uint32_t &value) {
  return s_.ReadWord(block_ + off, value);
}

bool ExternalFlash::Open(const TargetImage &loader, std::string *error) {
  if (open_)
    return Fail(error, "already open");
  if (!loader.Symbol("loaderService", block_))
    return Fail(error, loader.Path() + " has no Serve() service block "
                       "(an older loader: use the ST entry points)");
  if (!call_.Download(loader, error))
    return false;
  // The block is in .bss, which is not downloaded, and SRAM survives a reset:
  // a magic left by an earlier session would read as ready before Serve()
  // has cleared it. Zero it first.
  const std::vector<uint8_t> zero((sizeof(LoaderServiceBlock) + 3) & ~3U, 0);
  if (!s_.Write(block_, zero.data(), zero.size()))
    return Fail(error, "could not clear the service block");
  if (!call_.Start("Serve", {call_.BufferAddress(), call_.BufferSize()}, error))
    return false;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kReadyTimeoutMs);
  uint32_t magic = 0;
  while (!(Field(kMagic, magic) && magic == LOADER_SERVICE_MAGIC)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      s_.Halt();
      return Fail(error, "Serve() did not become ready");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  uint32_t version = 0, status = 0, d[8] = {};
  if (!Field(kVersion, version) || !Field(kStatus, status) ||
      !Field(kSeq, seq_)) {
    s_.Halt();
    return Fail(error, "could not read the service block");
  }
  for (int i = 0; i < 8; i++)
    Field(kDetail + 4 * i, d[i]);
  jedec_ = d[LOADER_DETAIL_JEDEC];
  sr1_ = d[LOADER_DETAIL_SR1];
  size_ = d[LOADER_DETAIL_SIZE];
  sector_ = d[LOADER_DETAIL_SECTOR];
  writable_ = d[LOADER_DETAIL_WRITABLE] != 0;

  if (version != LOADER_SERVICE_VERSION) {
    s_.Halt();
    return Fail(error, "service block version " + std::to_string(version) +
                           ", expected " + std::to_string(LOADER_SERVICE_VERSION));
  }
  if (static_cast<int32_t>(status) != LOADER_STATUS_OK) {
    uint32_t r0 = 0;
    call_.WaitReturn(r0, 100);
    char jedec[16];
    std::snprintf(jedec, sizeof jedec, "0x%06X", jedec_);
    return Fail(error, std::string(StatusName(static_cast<int32_t>(status))) +
                           " (JEDEC " + jedec + ")");
  }
  open_ = true;
  return true;
}

bool ExternalFlash::Command(uint32_t cmd, uint32_t offset, uint32_t length,
                            int timeout_ms, std::string *error) {
  if (!open_)
    return Fail(error, "not open");
  // Fields first; seq last, because writing seq submits the command.
  if (!s_.WriteWord(block_ + kCmd, cmd) ||
      !s_.WriteWord(block_ + kOffset, offset) ||
      !s_.WriteWord(block_ + kLength, length) ||
      !s_.WriteWord(block_ + kSeq, ++seq_))
    return Fail(error, "could not submit the command");

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  uint32_t ack = 0;
  while (!(Field(kAck, ack) && ack == seq_)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      s_.Halt();
      open_ = false;
      return Fail(error, "command " + std::to_string(cmd) +
                             " not acknowledged within " +
                             std::to_string(timeout_ms) + " ms; core halted");
    }
  }
  uint32_t status = 0;
  if (!Field(kStatus, status))
    return Fail(error, "could not read the command status");
  if (static_cast<int32_t>(status) != LOADER_STATUS_OK) {
    uint32_t at = 0;
    Field(kDetail + 4 * LOADER_DETAIL_FAIL_OFFSET, at);
    return Fail(error, std::string(StatusName(static_cast<int32_t>(status))) +
                           " at offset " + std::to_string(at ? at : offset));
  }
  return true;
}

bool ExternalFlash::Read(uint32_t offset, uint8_t *out, uint32_t len,
                         const Progress &progress, std::string *error) {
  const uint32_t buf = call_.BufferAddress(), max = call_.BufferSize();
  for (uint32_t done = 0; done < len;) {
    const uint32_t n = std::min(max, len - done);
    if (!Command(LOADER_CMD_READ, offset + done, n, kReadTimeoutMs, error))
      return false;
    std::vector<uint8_t> tmp((n + 3) & ~3U);
    if (!s_.Read(buf, tmp.data(), tmp.size(), 8192, nullptr))
      return Fail(error, "SWD read of the buffer failed");
    std::copy(tmp.begin(), tmp.begin() + n, out + done);
    done += n;
    if (progress)
      progress(done, len);
  }
  return true;
}

bool ExternalFlash::EraseSector(uint32_t offset, std::string *error) {
  return Command(LOADER_CMD_ERASE_SECTOR, offset, 0, kEraseTimeoutMs, error);
}

bool ExternalFlash::Program(uint32_t offset, const uint8_t *data, uint32_t len,
                            std::string *error) {
  const uint32_t buf = call_.BufferAddress(), max = call_.BufferSize();
  for (uint32_t done = 0; done < len;) {
    const uint32_t n = std::min(max, len - done);
    std::vector<uint8_t> tmp((n + 3) & ~3U, 0xFF);
    std::copy(data + done, data + done + n, tmp.begin());
    if (!s_.Write(buf, tmp.data(), tmp.size()))
      return Fail(error, "SWD write of the buffer failed");
    if (!Command(LOADER_CMD_PROGRAM, offset + done, n, kProgramTimeoutMs, error))
      return false;
    done += n;
  }
  return true;
}

bool ExternalFlash::Close(std::string *error) {
  if (!open_)
    return true;
  open_ = false;
  if (!s_.WriteWord(block_ + kCmd, LOADER_CMD_EXIT) ||
      !s_.WriteWord(block_ + kSeq, ++seq_)) {
    s_.Halt();
    return Fail(error, "could not send EXIT");
  }
  uint32_t r0 = 0;
  return call_.WaitReturn(r0, 1000, error) && r0 == 1;
}

} // namespace tagcore::recovery
