/**
 * @file    tag-capture.cc
 * @brief   Capture a tag's registers, internal flash and SRAM over SWD
 *          without booting its firmware.
 *
 * @details The first thing to run on a returned tag, before tag-info or any
 *          other monitor tool: a monitor attach lets the firmware boot, which
 *          clears the reset flags and can rewrite pState and the marker log.
 *          Writes captures/capture-YYYYmmdd-HHMMSS/ (UTC) with one file per region and a
 *          manifest.json: registers (the backup registers above all), option
 *          bytes, OTP and internal flash; SRAM only with --sram; then the
 *          external flash, through the loader the tag's identity record names
 *          (found under --loader-dir, default $TAG_LOADER_DIR and
 *          build-host/embedded/loaders), or --loader. --no-external skips it.
 *          For SPI NAND, blank blocks are skipped (--external-full reads
 *          them).
 *
 *          Exit status: 0 complete, 2 written but some region failed,
 *          1 no capture.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md
 */

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <cxxopts.hpp>

extern "C" {
#include "log.h"
}

#include "recovery/statecapture.h"

using namespace tagcore::recovery;

namespace {

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
      std::cerr << "several bases found; choose one with --base bus:device" << std::endl;
      for (const UsbDev &d : devs)
        std::cerr << "  " << unsigned(d.bus) << ":" << unsigned(d.address) << std::endl;
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

} // namespace

/**
 * @brief   Parse options, select a base, capture, and print a summary.
 * @return  0 complete, 2 written with failed regions, 1 no capture.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-capture",
                           "Capture a tag's state over SWD without booting it");
  options.add_options()
      ("o,out-dir", "Parent directory for the capture",
       cxxopts::value<std::string>()->default_value("captures"))
      ("r,reason", "Why the capture was taken, recorded in the manifest",
       cxxopts::value<std::string>()->default_value(""))
      ("sram", "Also capture SRAM (rarely useful: Shutdown and Standby lose it)")
      ("leave-halted", "Leave the core halted instead of resetting the tag")
      ("no-external", "Skip the external flash")
      ("loader", "Loader image for the external flash (default: the one the "
                 "identity record names)", cxxopts::value<std::string>())
      ("loader-dir", "Directory searched for <loader>.stldr (repeatable)",
       cxxopts::value<std::vector<std::string>>())
      ("external-full", "SPI NAND: read every block, blank or not")
      ("b,base", "Select bus:device", cxxopts::value<std::string>())
      ("d,debug", "Set log level to DEBUG")
      ("h,help", "Print usage");

  CaptureOptions copt;
  std::string base;
  try {
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      return 0;
    }
    log_set_level(result.count("debug") ? LOG_DEBUG : LOG_ERROR);
    copt.parent_dir = result["out-dir"].as<std::string>();
    copt.reason = result["reason"].as<std::string>();
    copt.include_sram = result.count("sram") > 0;
    copt.exit = result.count("leave-halted") ? SwdExit::LeaveHalted
                                             : SwdExit::HardwareReset;
    copt.include_external = result.count("no-external") == 0;
    copt.external_full = result.count("external-full") > 0;
    if (result.count("loader"))
      copt.loader_path = result["loader"].as<std::string>();
    if (result.count("loader-dir"))
      copt.loader_dirs = result["loader-dir"].as<std::vector<std::string>>();
    if (const char *env = std::getenv("TAG_LOADER_DIR"))
      copt.loader_dirs.push_back(env);
    copt.loader_dirs.push_back("build-host/embedded/loaders");
    if (result.count("base"))
      base = result["base"].as<std::string>();
  } catch (const cxxopts::OptionException &e) {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    return 1;
  }
  copt.progress = [](const std::string &line) { std::cout << "  " << line << std::endl; };

  UsbDev dev;
  if (!SelectBase(base, dev))
    return 1;

  CaptureResult r;
  const bool written = CaptureTag(copt, r, dev);
  if (!written) {
    std::cerr << "capture failed: " << r.error << std::endl;
    if (r.attach.idcode)
      std::fprintf(stderr, "  halted=%d pc=0x%08X reset_vector=0x%08X\n",
                   r.attach.halted, r.attach.pc, r.attach.reset_vector);
    return 1;
  }

  std::printf("\n%s  uid %s  %.2f V\n", r.mcu.c_str(), r.uid.c_str(), r.attach.voltage);
  std::printf("halted at reset vector 0x%08X (firmware did not run)\n", r.attach.pc);
  for (const CapturedRegion &g : r.regions) {
    std::printf("  %-16s %8u bytes  %-4s %6.2f s %8.0f B/s  %.16s...\n",
                g.name.c_str(), g.size, g.ok ? "ok" : "FAIL", g.seconds,
                g.seconds > 0 ? g.size / g.seconds : 0.0, g.sha256.c_str());
    for (const AddressRange &f : g.failed)
      std::printf("      unreadable 0x%08X +%u\n", f.first, f.second);
  }
  if (r.identity.found)
    std::printf("  identity: %s on %s, loader %s, git %.8s\n",
                r.identity.String("target").c_str(), r.identity.String("board").c_str(),
                r.identity.String("loader").c_str(), r.identity.String("git_sha").c_str());
  if (r.external_attempted) {
    const ExternalCaptureResult &x = r.external;
    std::printf("  external flash %s: JEDEC 0x%06X, %.1f s", x.ok ? "ok" : "FAIL",
                x.jedec, x.seconds);
    if (x.paged)
      std::printf(", %u blocks read, %u blank, %u uncorrectable pages", x.blocks_read,
                  x.blocks_blank, x.pages_uncorrectable);
    std::printf("\n");
    if (!x.error.empty())
      std::printf("      %s\n", x.error.c_str());
  }
  if (!r.external_note.empty())
    std::printf("  external flash: %s\n", r.external_note.c_str());
  std::printf("%s\n%s\n", r.complete ? "complete" : "INCOMPLETE", r.manifest.c_str());
  return r.complete ? 0 : 2;
}
