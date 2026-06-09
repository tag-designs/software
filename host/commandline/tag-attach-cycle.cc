#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include <cxxopts.hpp>
#include <tag.pb.h>
#include <tagclass.h>

extern "C"
{
#include "log.h"
}

extern bool parse_options(int argc, char **argv,
                          cxxopts::Options &options,
                          Tag &tag,
                          UsbDev &dev);

using MS = std::chrono::milliseconds;

static std::atomic<bool> stop_requested(false);

static void intHandler(int)
{
  stop_requested.store(true);
}

static void interruptibleSleep(MS duration)
{
  const MS step(100);
  auto remaining = duration;

  while (!stop_requested.load() && remaining.count() > 0) {
    const MS nap = remaining < step ? remaining : step;
    std::this_thread::sleep_for(nap);
    remaining -= nap;
  }
}

static void printStatus(uint32_t cycle, const Status &status)
{
  std::cout << "cycle " << cycle
            << ": state=" << TagState_Name(status.state())
            << " millis=" << status.millis()
            << " internal=" << status.internal_data_count()
            << " external=" << status.external_data_count()
            << std::endl;

  if (!status.debug_message().empty()) {
    std::cerr << status.debug_message();
  }
}

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;
  uint32_t on_ms = 5000;
  uint32_t off_ms = 5000;
  uint32_t cycles = 0;
  bool no_status = false;

  cxxopts::Options options(
      "tag-attach-cycle",
      "repeatedly attach and detach from a running tag");

  options.add_options()
      ("on-ms", "Milliseconds to remain attached",
       cxxopts::value<uint32_t>(on_ms)->default_value("5000"))
      ("off-ms", "Milliseconds to remain detached",
       cxxopts::value<uint32_t>(off_ms)->default_value("5000"))
      ("cycles", "Attach/detach cycle count; 0 runs until Ctrl-C",
       cxxopts::value<uint32_t>(cycles)->default_value("0"))
      ("no-status", "Do not read tag status after attaching",
       cxxopts::value<bool>(no_status)->default_value("false"));

  signal(SIGINT, intHandler);

  if (!parse_options(argc, argv, options, tag, dev)) {
    return 1;
  }

  for (uint32_t cycle = 1;
       !stop_requested.load() && ((cycles == 0U) || (cycle <= cycles));
       cycle++) {
    std::cout << "cycle " << cycle << ": attach" << std::endl;
    if (!tag.Attach(dev)) {
      std::cerr << "cycle " << cycle << ": attach failed" << std::endl;
      return 1;
    }

    if (!no_status) {
      Status status;
      if (tag.GetStatus(status)) {
        printStatus(cycle, status);
      } else {
        std::cerr << "cycle " << cycle << ": status failed" << std::endl;
      }
    }

    interruptibleSleep(MS(on_ms));

    std::cout << "cycle " << cycle << ": detach" << std::endl;
    tag.Detach();

    if ((cycles == 0U) || (cycle < cycles)) {
      interruptibleSleep(MS(off_ms));
    }
  }

  tag.Detach();
  return 0;
}
