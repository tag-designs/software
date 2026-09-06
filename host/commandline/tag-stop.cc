#include <chrono>
#include <thread>
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

  int stop_timeout_s = 30;

  cxxopts::Options options("tag-stop",
                           "stop a running tag and print the resulting status");
  options.add_options()
      ("stop-timeout",
       "Seconds to wait for the tag to actually reach a stopped state. The "
       "stop request is posted and acknowledged before the state machine acts "
       "on it",
       cxxopts::value<int>(stop_timeout_s)->default_value("30"));

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

  /*
   * Stop is posted, not performed. The monitor handler sets MON_WORK_STOP and
   * acks OK immediately; the state machine makes the transition later, and
   * under load -- an attach storm, a flash write in progress -- that takes
   * long enough for the status read below to still say RUNNING. The tool then
   * exited 0 having reported a stop that had not happened, and a download
   * issued next refused with "Can't dump logs from current state" because the
   * tag really was still running. That is the intermittent download failure.
   *
   * Poll for a terminal state, the way tag-reset already polls for IDLE after
   * Erase() for exactly the same reason.
   */
  Status status;
  /*
   * Seconds between tries, not milliseconds. Each poll is a monitor request
   * the tag has to service, so hammering the link competes with the very work
   * being waited on. A handful of well-spaced tries is both gentler and more
   * likely to catch the transition than hundreds of rapid ones.
   */
  const int poll_ms = 2000;
  const int tries = (stop_timeout_s * 1000 + poll_ms - 1) / poll_ms;
  bool stopped = false;
  for (int i = 0; i < tries; i++) {
    if (!tag.GetStatus(status)) {
      std::cerr << "Could not read tag status: " << tag.DebugMessage()
                << std::endl;
      return 1;
    }
    if (status.state() == FINISHED || status.state() == ABORTED) {
      stopped = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
  }
  if (!stopped) {
    std::cerr << "Tag did not reach a stopped state within " << stop_timeout_s
              << " s; last state " << TagState_Name(status.state())
              << std::endl;
    std::cout << status.DebugString() << std::endl;
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
