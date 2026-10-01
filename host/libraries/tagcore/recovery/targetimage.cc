/**
 * @file    targetimage.cc
 * @brief   Minimal ELF32 reader; contract in targetimage.h.
 */

#include "recovery/targetimage.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace tagcore::recovery {

namespace {

constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kShtSymtab = 2;
constexpr uint16_t kEmArm = 40;

uint16_t U16(const std::vector<uint8_t> &b, size_t o) {
  return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

uint32_t U32(const std::vector<uint8_t> &b, size_t o) {
  return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
         (static_cast<uint32_t>(b[o + 2]) << 16) |
         (static_cast<uint32_t>(b[o + 3]) << 24);
}

bool Fail(std::string *error, const std::string &why) {
  if (error)
    *error = why;
  return false;
}

} // namespace

bool TargetImage::Load(const std::string &path, std::string *error) {
  path_ = path;
  segments_.clear();
  symbols_.clear();

  std::ifstream in(path, std::ios::binary);
  if (!in)
    return Fail(error, "cannot open " + path);
  const std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());

  if (f.size() < 52 || std::memcmp(f.data(), "\x7f" "ELF", 4) != 0)
    return Fail(error, path + " is not an ELF file");
  if (f[4] != 1 || f[5] != 1)
    return Fail(error, path + " is not ELF32 little-endian");
  if (U16(f, 18) != kEmArm)
    return Fail(error, path + " is not an ARM image");

  const uint32_t phoff = U32(f, 28), shoff = U32(f, 32);
  const uint16_t phentsize = U16(f, 42), phnum = U16(f, 44);
  const uint16_t shentsize = U16(f, 46), shnum = U16(f, 48);
  auto in_file = [&](uint64_t off, uint64_t len) { return off + len <= f.size(); };

  if (!in_file(phoff, uint64_t(phentsize) * phnum) || phentsize < 32)
    return Fail(error, path + ": program headers out of range");
  for (uint16_t i = 0; i < phnum; i++) {
    const size_t p = phoff + size_t(i) * phentsize;
    if (U32(f, p) != kPtLoad)
      continue;
    const uint32_t offset = U32(f, p + 4), paddr = U32(f, p + 12);
    const uint32_t filesz = U32(f, p + 16), memsz = U32(f, p + 20);
    if (memsz == 0)
      continue;
    if (!in_file(offset, filesz) || filesz > memsz)
      return Fail(error, path + ": segment out of range");
    ImageSegment seg;
    seg.addr = paddr;
    seg.memsz = memsz;
    seg.data.assign(f.begin() + offset, f.begin() + offset + filesz);
    segments_.push_back(std::move(seg));
  }

  if (!in_file(shoff, uint64_t(shentsize) * shnum) || shentsize < 40)
    return Fail(error, path + ": section headers out of range");
  for (uint16_t i = 0; i < shnum; i++) {
    const size_t s = shoff + size_t(i) * shentsize;
    if (U32(f, s + 4) != kShtSymtab)
      continue;
    const uint32_t off = U32(f, s + 16), size = U32(f, s + 20);
    const uint32_t link = U32(f, s + 24), entsize = U32(f, s + 36);
    if (entsize < 16 || link >= shnum || !in_file(off, size))
      return Fail(error, path + ": malformed symbol table");
    const size_t strh = shoff + size_t(link) * shentsize;
    const uint32_t stroff = U32(f, strh + 16), strsize = U32(f, strh + 20);
    if (!in_file(stroff, strsize))
      return Fail(error, path + ": malformed string table");
    for (uint32_t e = 0; e + entsize <= size; e += entsize) {
      const uint32_t name = U32(f, off + e), value = U32(f, off + e + 4);
      if (name == 0 || name >= strsize)
        continue;
      const char *n = reinterpret_cast<const char *>(f.data() + stroff + name);
      const size_t max = strsize - name;
      symbols_.emplace(std::string(n, strnlen(n, max)), value);
    }
  }
  if (symbols_.empty())
    return Fail(error, path + " has no symbol table");
  return true;
}

bool TargetImage::Symbol(const std::string &name, uint32_t &value) const {
  const auto it = symbols_.find(name);
  if (it == symbols_.end())
    return false;
  value = it->second;
  return true;
}

bool TargetImage::ReadAt(uint32_t addr, void *buf, uint32_t len) const {
  for (const ImageSegment &s : segments_) {
    if (addr >= s.addr && uint64_t(addr) + len <= uint64_t(s.addr) + s.data.size()) {
      std::memcpy(buf, s.data.data() + (addr - s.addr), len);
      return true;
    }
  }
  return false;
}

} // namespace tagcore::recovery
