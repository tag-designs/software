/**
 * @file    externalcapture.cc
 * @brief   External-flash capture; contract in externalcapture.h.
 */

#include "recovery/externalcapture.h"

#include "recovery/externalflash.h"
#include "recovery/sha256.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace tagcore::recovery {

namespace {

/** @brief Writes a file and hashes it as it goes. */
class HashedFile {
public:
  HashedFile(const std::string &dir, const std::string &file)
      : path_(dir + "/" + file), file_(file), f_(std::fopen(path_.c_str(), "wb")) {}
  ~HashedFile() {
    if (f_)
      std::fclose(f_);
  }
  bool Ok() const { return f_ != nullptr; }
  bool Write(const uint8_t *data, size_t n) {
    sha_.Update(data, n);
    size_ += n;
    return std::fwrite(data, 1, n, f_) == n;
  }
  ExternalCaptureFile Finish(const std::string &name) {
    if (f_) {
      std::fclose(f_);
      f_ = nullptr;
    }
    return {name, file_, size_, sha_.HexDigest()};
  }

private:
  std::string path_, file_;
  std::FILE *f_;
  Sha256 sha_;
  uint64_t size_ = 0;
};

void Progress(const ExternalCaptureOptions &o, const std::string &line) {
  if (o.progress)
    o.progress(line);
}

std::string FileSha256(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
  return Sha256::Of(bytes.data(), bytes.size());
}

bool Fail(ExternalCaptureResult &r, const std::string &why) {
  r.error = why;
  r.ok = false;
  return false;
}

bool CaptureLinear(ExternalFlash &xf, const std::string &dir,
                   const ExternalCaptureOptions &o, ExternalCaptureResult &r) {
  HashedFile out(dir, o.prefix + "flash.bin");
  if (!out.Ok())
    return Fail(r, "cannot write to " + dir);
  constexpr uint32_t kChunk = 64 * 1024;
  std::vector<uint8_t> buf(kChunk);
  std::string err;
  uint32_t next_report = 0;
  for (uint32_t done = 0; done < r.size;) {
    const uint32_t n = std::min(kChunk, r.size - done);
    if (!xf.Read(done, buf.data(), n, nullptr, &err))
      return Fail(r, "read at offset " + std::to_string(done) + ": " + err);
    if (!out.Write(buf.data(), n))
      return Fail(r, "write to " + dir + " failed");
    done += n;
    if (done >= next_report || done == r.size) {
      Progress(o, std::to_string(done) + " / " + std::to_string(r.size) + " bytes");
      next_report = done + 512 * 1024;
    }
  }
  r.files.push_back(out.Finish("external_flash"));
  return true;
}

bool CapturePaged(ExternalFlash &xf, const std::string &dir,
                  const ExternalCaptureOptions &o, ExternalCaptureResult &r) {
  r.paged = true;
  r.page_bytes = xf.PageBytes();
  uint32_t data_bytes = 1; // the data area: the largest power of 2 in a page
  while (data_bytes * 2 <= r.page_bytes)
    data_bytes *= 2;
  r.pages_per_block = xf.SectorSize() / data_bytes;
  const uint32_t blocks = xf.Size() / xf.SectorSize();
  uint32_t count = o.block_count;
  if (count == 0 || o.first_block + count > blocks)
    count = o.first_block < blocks ? blocks - o.first_block : 0;

  HashedFile raw(dir, o.prefix + "raw.bin"), ecc(dir, o.prefix + "ecc.bin"),
      csv(dir, o.prefix + "pages.csv");
  if (!raw.Ok() || !ecc.Ok() || !csv.Ok())
    return Fail(r, "cannot write to " + dir);
  const std::string head = "page,block,raw_c0,ecc_c0,ecc_f0,ecc_verdict\n";
  csv.Write(reinterpret_cast<const uint8_t *>(head.data()), head.size());

  static const char *kVerdict[4] = {"ok", "corrected", "uncorrectable", "corrected8"};
  std::vector<uint8_t> page(r.page_bytes), page_ecc(r.page_bytes);
  std::string err;
  for (uint32_t b = o.first_block; b < o.first_block + count; b++) {
    const uint32_t p0 = b * r.pages_per_block;
    uint8_t st = 0, st2 = 0;
    if (!xf.ReadPage(p0, true, page.data(), st, st2, &err))
      return Fail(r, "page " + std::to_string(p0) + " (raw): " + err);
    r.blocks_scanned++;
    const bool blank = std::all_of(page.begin(), page.end(),
                                   [](uint8_t v) { return v == 0xFF; });
    if (blank && !o.full) {
      r.blocks_blank++;
    } else {
      if (page[data_bytes] != 0xFF) // spare byte 0 of page 0: factory mark
        r.blocks_marked++;
      r.blocks_read++;
      for (uint32_t i = 0; i < r.pages_per_block; i++) {
        const uint32_t pg = p0 + i;
        uint8_t raw_st = st;
        if (i > 0 && !xf.ReadPage(pg, true, page.data(), raw_st, st2, &err))
          return Fail(r, "page " + std::to_string(pg) + " (raw): " + err);
        uint8_t ecc_st = 0, ecc_st2 = 0;
        if (!xf.ReadPage(pg, false, page_ecc.data(), ecc_st, ecc_st2, &err))
          return Fail(r, "page " + std::to_string(pg) + " (ECC): " + err);
        const unsigned verdict = (ecc_st >> 4) & 3U;
        if (verdict == 2U)
          r.pages_uncorrectable++;
        raw.Write(page.data(), r.page_bytes);
        ecc.Write(page_ecc.data(), r.page_bytes);
        char line[96];
        const int n = std::snprintf(line, sizeof(line), "%u,%u,0x%02X,0x%02X,0x%02X,%s\n",
                                    pg, b, raw_st, ecc_st, ecc_st2, kVerdict[verdict]);
        csv.Write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(n));
      }
    }
    if ((b - o.first_block) % 64 == 63 || b + 1 == o.first_block + count)
      Progress(o, "block " + std::to_string(b + 1 - o.first_block) + "/" +
                      std::to_string(count) + "  read " +
                      std::to_string(r.blocks_read) + "  blank " +
                      std::to_string(r.blocks_blank));
  }
  r.files.push_back(raw.Finish("raw"));
  r.files.push_back(ecc.Finish("ecc"));
  r.files.push_back(csv.Finish("pages"));
  return true;
}

} // namespace

bool CaptureExternalFlash(SwdSession &s, const TargetImage &loader,
                          const std::string &dir,
                          const ExternalCaptureOptions &o,
                          ExternalCaptureResult &r) {
  r = ExternalCaptureResult();
  r.loader_path = loader.Path();
  r.loader_sha256 = FileSha256(loader.Path());
  const auto t0 = std::chrono::steady_clock::now();

  ExternalFlash xf(s);
  std::string err;
  if (!xf.Open(loader, &err))
    return Fail(r, "loader failed: " + err);
  r.version = xf.Version();
  r.jedec = xf.Jedec();
  r.sr1 = xf.Sr1();
  r.found = xf.Found();
  r.size = xf.Size();

  const bool read_ok = xf.PageBytes() ? CapturePaged(xf, dir, o, r)
                                      : CaptureLinear(xf, dir, o, r);
  std::string close_err;
  const bool closed = xf.Close(&close_err);
  r.seconds = std::chrono::duration<double>(
                  std::chrono::steady_clock::now() - t0).count();
  if (!read_ok)
    return false;
  if (!closed)
    return Fail(r, "Serve() did not exit cleanly (configuration not restored?): " +
                       close_err);
  r.ok = true;
  return true;
}

} // namespace tagcore::recovery
