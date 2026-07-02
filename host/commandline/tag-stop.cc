#include <cstdlib>
#include <iostream>
#include <string>

#include <cxxopts.hpp>
#include <tag.pb.h>
#include <tagclass.h>

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern "C"
{
#include "log.h"
}

extern bool parse_options(int argc, char **argv,
                          cxxopts::Options &options,
                          Tag &tag,
                          UsbDev &dev);

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;

  cxxopts::Options options("tag-stop",
                           "stop a running tag and print the resulting status");

  if (!parse_options(argc, argv, options, tag, dev)) {
    return 1;
  }

  if (!tag.Attach(dev)) {
    std::cerr << "Attach failed" << std::endl;
    return 1;
  }

  signal(SIGINT, intHandler);

  if (!tag.Stop()) {
    const std::string message = tag.DebugMessage();
    if (!message.empty()) {
      std::cerr << message;
      if (message.back() != '\n') {
        std::cerr << std::endl;
      }
    }
    std::cerr << "Stop failed" << std::endl;
    return 1;
  }

  Status status;
  if (!tag.GetStatus(status)) {
    std::cerr << "Could not read tag status" << std::endl;
    return 1;
  }

  if (!status.debug_message().empty()) {
    std::cerr << status.debug_message();
    if (status.debug_message().back() != '\n') {
      std::cerr << std::endl;
    }
  }

  std::cout << status.DebugString() << std::endl;
  return 0;
}
