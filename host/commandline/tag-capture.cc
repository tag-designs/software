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
 *          bytes, OTP and internal flash; SRAM only with --sram. External
 *          flash is not captured yet.
 *
 *          Exit status: 0 complete, 2 written but some region failed,
 *          1 no capture.
 *
 * @see     host/libraries/tagcore/design/swd-recovery.md
 */

#include <cstdio>
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
  std::printf("%s\n%s\n", r.complete ? "complete" : "INCOMPLETE", r.manifest.c_str());
  return r.complete ? 0 : 2;
}
