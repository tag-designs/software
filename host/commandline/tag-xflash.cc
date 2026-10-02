/**
 * @file    tag-xflash.cc
 * @brief   Read a tag's external flash over SWD through an SRAM-resident
 *          loader, without booting the tag's firmware.
 *
 * @details `tag-xflash dump --loader <image.stldr> -o <file>` halts the tag at
 *          its reset vector (as tag-capture does), downloads the loader into
 *          SRAM1, calls its STM32CubeProgrammer entry points `Init` and `Read`
 *          from the host, and streams the part to a file. No code from
 *          internal flash runs, and internal flash is not written.
 *
 *          A loader with a `Serve()` entry point is driven through it
 *          (ExternalFlash, step 4 of the SWD recovery sequence), which also
 *          reports the part's JEDEC ID and status register as found. Any
 *          other `.stldr`, ST's own included, is driven through the generic
 *          ST-style calls (step 3); `--st` forces that path.
 *
 *          The loader's own descriptor (`StorageInfo`, the ELF segment at
 *          address 0) gives the part's base address and size.
 *
 *          Exit status: 0 the whole range was read, 1 otherwise.
 *
 * @warning Downloading the loader overwrites the start of SRAM1. Run
 *          tag-capture first if SRAM matters.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md
 * @see     embedded/loaders/design/loader-runtime.md
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <cxxopts.hpp>

extern "C" {
#include "log.h"
}

#include "recovery/externalcapture.h"
#include "recovery/externalflash.h"
#include "recovery/sramcall.h"
#include "recovery/swdsession.h"
#include "recovery/targetimage.h"

using namespace tagcore::recovery;

namespace {

/// Offsets in the loader's StorageInfo (embedded/loaders/common/inc/dev_inf.h),
/// as laid out by arm-none-eabi-gcc: char[100], u16, then 32-bit fields.
constexpr uint32_t kInfoName = 0, kInfoStart = 104, kInfoSize = 108,
                   kInfoPage = 112;

/// Longest a single Init or Read call may take, in milliseconds.
constexpr int kInitTimeoutMs = 2000;
constexpr int kReadTimeoutMs = 5000;

/// Resolve "bus:device", or the only base present, to a UsbDev.
bool SelectBase(const std::string &spec, UsbDev &dev) {
  SwdSession probe;
  std::vector<UsbDev> devs;
  probe.Available(devs);
  if (devs.empty()) {
    std::cerr << "no base found" << std::endl;
    return false;
  }
  if (spec.empty()) {
    if (devs.size() > 1) {
      std::cerr << "several bases found; choose one with --base bus:device"
                << std::endl;
      return false;
    }
    dev = devs[0];
    return true;
  }
  unsigned bus = 0, address = 0;
  if (std::sscanf(spec.c_str(), "%u:%u", &bus, &address) != 2) {
    std::cerr << "--base expects bus:device" << std::endl;
    return false;
  }
  for (const UsbDev &d : devs)
    if (d.bus == bus && d.address == address) {
      dev = d;
      return true;
    }
  std::cerr << "no base at " << spec << std::endl;
  return false;
}

/**
 * @brief   Read the part through the loader into @p out.
 * @return  true when [offset, offset + length) was read in full.
 */
bool Dump(SwdSession &s, const TargetImage &loader, uint32_t offset,
          uint32_t length, bool length_given, std::FILE *out) {
  uint32_t base = 0, size = 0, page = 0;
  char name[101] = {};
  if (!loader.ReadAt(kInfoStart, &base, 4) || !loader.ReadAt(kInfoSize, &size, 4) ||
      !loader.ReadAt(kInfoPage, &page, 4) || !loader.ReadAt(kInfoName, name, 100)) {
    std::cerr << loader.Path() << " has no StorageInfo descriptor at address 0"
              << std::endl;
    return false;
  }
  if (!length_given)
    length = size > offset ? size - offset : 0;
  if (uint64_t(offset) + length > size) {
    std::cerr << "range runs past the part's " << size << " bytes" << std::endl;
    return false;
  }
  std::printf("loader  %s\n        %s, %u bytes at 0x%08X\n", loader.Path().c_str(),
              name, size, base);

  SramCall call(s);
  std::string err;
  if (!call.Download(loader, &err)) {
    std::cerr << "download failed: " << err << std::endl;
    return false;
  }
  std::printf("        stack 0x%08X, buffer 0x%08X + %u\n", call.StackTop(),
              call.BufferAddress(), call.BufferSize());

  uint32_t r0 = 0;
  if (!call.Call("Init", {}, r0, kInitTimeoutMs, &err) || r0 != 1) {
    std::cerr << "Init failed" << (err.empty() ? " (returned 0)" : ": " + err)
              << std::endl;
    return false;
  }

  std::vector<uint8_t> buf(call.BufferSize());
  const auto t0 = std::chrono::steady_clock::now();
  uint32_t done = 0, next_report = 0;
  while (done < length) {
    const uint32_t n = std::min(call.BufferSize(), length - done);
    // Read(Address, Size, buffer): the address is the loader's fictional one.
    if (!call.Call("Read", {base + offset + done, n, call.BufferAddress()}, r0,
                   kReadTimeoutMs, &err) ||
        r0 != 1) {
      std::cerr << "\nRead at offset " << offset + done << " failed"
                << (err.empty() ? " (returned 0)" : ": " + err) << std::endl;
      return false;
    }
    std::vector<AddressRange> failed;
    if (!s.Read(call.BufferAddress(), buf.data(), (n + 3) & ~3U, 8192, &failed)) {
      std::cerr << "\nSWD read of the buffer failed at offset " << offset + done
                << std::endl;
      return false;
    }
    if (std::fwrite(buf.data(), 1, n, out) != n) {
      std::cerr << "\nwrite to the output file failed" << std::endl;
      return false;
    }
    done += n;
    if (done >= next_report || done == length) {
      const double secs = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t0).count();
      std::printf("\r        %u / %u bytes  %.0f B/s", done, length,
                  secs > 0 ? done / secs : 0.0);
      std::fflush(stdout);
      next_report = done + 256 * 1024;
    }
  }
  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("\n        %u bytes in %.1f s\n", length, secs);
  return true;
}

/**
 * @brief   Read the part through the loader's Serve() entry point.
 * @return  true when [offset, offset + length) was read in full.
 */
bool DumpServe(SwdSession &s, const TargetImage &loader, uint32_t offset,
               uint32_t length, bool length_given, std::FILE *out) {
  ExternalFlash xf(s);
  std::string err;
  if (!xf.Open(loader, &err)) {
    std::cerr << "loader failed: " << err << std::endl;
    return false;
  }
  std::printf("loader  %s (Serve)\n        JEDEC 0x%06X  SR1 0x%02X  %u bytes, "
              "%u-byte sectors, %s\n",
              loader.Path().c_str(), xf.Jedec(), xf.Sr1(), xf.Size(),
              xf.SectorSize(), xf.Writable() ? "read-write" : "read-only");
  if (!length_given)
    length = xf.Size() > offset ? xf.Size() - offset : 0;
  if (uint64_t(offset) + length > xf.Size()) {
    std::cerr << "range runs past the part's " << xf.Size() << " bytes" << std::endl;
    xf.Close();
    return false;
  }

  std::vector<uint8_t> data(length);
  const auto t0 = std::chrono::steady_clock::now();
  uint32_t next_report = 0;
  const bool ok = xf.Read(offset, data.data(), length,
      [&](uint32_t done, uint32_t total) {
        if (done >= next_report || done == total) {
          const double secs = std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - t0).count();
          std::printf("\r        %u / %u bytes  %.0f B/s", done, total,
                      secs > 0 ? done / secs : 0.0);
          std::fflush(stdout);
          next_report = done + 256 * 1024;
        }
      }, &err);
  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (!ok) {
    std::cerr << "\nread failed: " << err << std::endl;
    xf.Close();
    return false;
  }
  std::printf("\n        %u bytes in %.1f s\n", length, secs);
  if (std::fwrite(data.data(), 1, length, out) != length) {
    std::cerr << "write to the output file failed" << std::endl;
    xf.Close();
    return false;
  }
  if (!xf.Close(&err)) {
    std::cerr << "Serve() did not exit cleanly: " << err << std::endl;
    return false;
  }
  return true;
}

/**
 * @brief   Read a paged part (SPI NAND) into @p dir through the shared
 *          capture code (recovery/externalcapture.h): page 0 of each block
 *          raw, blank blocks skipped unless @p full, every other block raw and
 *          through ECC. Writes raw.bin, ecc.bin, pages.csv and summary.txt.
 *
 * @return  true when every requested block was read.
 */
bool DumpNand(SwdSession &s, const TargetImage &loader, const std::string &dir,
              uint32_t first_block, uint32_t block_count, bool full) {
  ExternalCaptureOptions o;
  o.full = full;
  o.first_block = first_block;
  o.block_count = block_count;
  o.progress = [](const std::string &line) {
    std::printf("\r        %s   ", line.c_str());
    std::fflush(stdout);
  };
  ExternalCaptureResult r;
  CaptureExternalFlash(s, loader, dir, o, r);
  std::printf("\nloader  %s (Serve v%u)\n        JEDEC 0x%06X  found A0=%02X B0=%02X "
              "C0=%02X F0=%02X\n",
              loader.Path().c_str(), r.version, r.jedec, r.found & 0xFF,
              (r.found >> 8) & 0xFF, (r.found >> 16) & 0xFF, r.found >> 24);
  if (!r.ok) {
    std::cerr << r.error << std::endl;
    return false;
  }
  if (!r.paged) {
    std::cerr << "this loader's part has no pages; use dump" << std::endl;
    return false;
  }
  std::printf("        %u blocks read (%u factory-marked), %u blank and skipped, "
              "%u uncorrectable pages, %.0f s\n", r.blocks_read, r.blocks_marked,
              r.blocks_blank, r.pages_uncorrectable, r.seconds);
  if (std::FILE *sum = std::fopen((dir + "/summary.txt").c_str(), "w")) {
    std::fprintf(sum, "loader %s\nloader_sha256 %s\nserve_version %u\njedec 0x%06X\n"
                      "found_A0 0x%02X\nfound_B0 0x%02X\nfound_C0 0x%02X\n"
                      "found_F0 0x%02X\npage_bytes %u\npages_per_block %u\n"
                      "blocks_scanned %u\nfirst_block %u\nblocks_read %u\n"
                      "blocks_blank %u\nblocks_factory_marked %u\n"
                      "pages_uncorrectable %u\nfull %d\n",
                 r.loader_path.c_str(), r.loader_sha256.c_str(), r.version, r.jedec,
                 r.found & 0xFF, (r.found >> 8) & 0xFF, (r.found >> 16) & 0xFF,
                 r.found >> 24, r.page_bytes, r.pages_per_block, r.blocks_scanned,
                 first_block, r.blocks_read, r.blocks_blank, r.blocks_marked,
                 r.pages_uncorrectable, full ? 1 : 0);
    for (const ExternalCaptureFile &f : r.files)
      std::fprintf(sum, "file %s %llu %s\n", f.file.c_str(),
                   static_cast<unsigned long long>(f.size), f.sha256.c_str());
    std::fclose(sum);
  }
  return true;
}

} // namespace

/**
 * @brief   Parse options and run the requested command.
 * @return  0 on success, 1 on any failure.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-xflash",
                           "Read a tag's external flash over SWD through a loader");
  options.positional_help("dump | nand");
  options.add_options()
      ("command", "Command: dump (linear data), or nand (whole pages, raw and ECC, into the --out directory)", cxxopts::value<std::string>())
      ("l,loader", "Loader image (.stldr or .elf)", cxxopts::value<std::string>())
      ("o,out", "Output file (dump) or existing directory (nand)", cxxopts::value<std::string>())
      ("offset", "First byte of the part to read",
       cxxopts::value<uint32_t>()->default_value("0"))
      ("length", "Bytes to read (default: to the end of the part)",
       cxxopts::value<uint32_t>())
      ("full", "nand: read every block, blank or not")
      ("first-block", "nand: first block to scan",
       cxxopts::value<uint32_t>()->default_value("0"))
      ("blocks", "nand: blocks to scan (default: to the end of the part)",
       cxxopts::value<uint32_t>()->default_value("0"))
      ("st", "Use the STM32CubeProgrammer entry points (Init, Read) even when "
             "the loader has Serve()")
      ("b,base", "Select bus:device", cxxopts::value<std::string>())
      ("d,debug", "Set log level to DEBUG")
      ("h,help", "Print usage");
  options.parse_positional({"command"});

  std::string command, loader_path, out_path, base;
  uint32_t offset = 0, length = 0;
  bool length_given = false;
  bool force_st = false, full = false;
  uint32_t first_block = 0, block_count = 0;
  try {
    auto result = options.parse(argc, argv);
    if (result.count("help") || !result.count("command")) {
      std::cout << options.help() << std::endl;
      return result.count("help") ? 0 : 1;
    }
    log_set_level(result.count("debug") ? LOG_DEBUG : LOG_ERROR);
    command = result["command"].as<std::string>();
    if (!result.count("loader") || !result.count("out")) {
      std::cerr << "dump needs --loader and --out" << std::endl;
      return 1;
    }
    loader_path = result["loader"].as<std::string>();
    out_path = result["out"].as<std::string>();
    offset = result["offset"].as<uint32_t>();
    if (result.count("length")) {
      length = result["length"].as<uint32_t>();
      length_given = true;
    }
    if (result.count("base"))
      base = result["base"].as<std::string>();
    force_st = result.count("st") > 0;
    full = result.count("full") > 0;
    first_block = result["first-block"].as<uint32_t>();
    block_count = result["blocks"].as<uint32_t>();
  } catch (const cxxopts::OptionException &e) {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    return 1;
  }
  if (command != "dump" && command != "nand") {
    std::cerr << "unknown command " << command << "; use dump or nand" << std::endl;
    return 1;
  }

  TargetImage loader;
  std::string err;
  if (!loader.Load(loader_path, &err)) {
    std::cerr << err << std::endl;
    return 1;
  }

  UsbDev dev;
  if (!SelectBase(base, dev))
    return 1;
  SwdSession s;
  if (!s.Open(dev)) {
    std::cerr << "could not halt the tag at its reset vector" << std::endl;
    return 1;
  }
  std::printf("%s halted at reset vector 0x%08X (firmware did not run)\n",
              s.Mcu()->name, s.AttachInfo().pc);

  if (command == "nand") {
    const bool ok = DumpNand(s, loader, out_path, first_block, block_count, full);
    s.Close(SwdExit::HardwareReset);
    std::printf("%s\n", ok ? "complete" : "FAILED");
    return ok ? 0 : 1;
  }

  std::FILE *out = std::fopen(out_path.c_str(), "wb");
  if (!out) {
    std::cerr << "cannot write " << out_path << std::endl;
    s.Close(SwdExit::HardwareReset);
    return 1;
  }
  uint32_t service = 0;
  const bool serve = !force_st && loader.Symbol("loaderService", service);
  const bool ok = serve ? DumpServe(s, loader, offset, length, length_given, out)
                        : Dump(s, loader, offset, length, length_given, out);
  std::fclose(out);
  s.Close(SwdExit::HardwareReset);
  std::printf("%s\n", ok ? "complete" : "FAILED");
  return ok ? 0 : 1;
}
