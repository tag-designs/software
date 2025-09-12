#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <tag.pb.h>
#include <tagclass.h>
#include <cxxopts.hpp>

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern "C"
{
#include "log.h"
}

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);
using namespace google::protobuf;
using MS = std::chrono::milliseconds;

bool rtcDrift(Tag &tag, float &f)
{
  int64_t millis;
  Status status;
  if (tag.GetStatus(status))
  {
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::time_point_cast<MS>(now);
    f = (status.millis() - ts.time_since_epoch().count()) / 1000.0;
    return true;
  }
  return false;
}

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;

  cxxopts::Options options("tag-test", "sets the RTC and executes tag self-tests");

  // Parse options

  if (parse_options(argc, argv, options,tag,dev) && tag.Attach(dev))
  {
    TagInfo info;
    std::string str;
    int size;
    float f;
    int epoch;
    int millis;
    std::list<State> state_log;

    // print floating point as  xxxxx.yy
    std::cout << std::fixed;
    std::cout.precision(2);

    // catch ctl-c
    signal(SIGINT, intHandler);

    // read tag information

    tag.GetTagInfo(info);

    // Write  Tag Information
    

    std::cout << "Tag Type: " << info.tag_type() <<std::endl;
    std::cout << "Board name: " << info.board_desc() << std::endl;
    std::cout << "Build time and date: " << info.build_time() << std::endl;
    std::cout << "Internal Flash size: " << info.intflashsz() << "kb" << std::endl;
    std::cout << "External Flash size: " << info.extflashsz() << "kb" << std::endl;
    std::cout << "Repo: " << info.gitrepo() << std::endl;
    std::cout << "Repo hash: " << info.githash() << std::endl;
    std::cout << "UUID: " << info.uuid() << std::endl;
    if (tag.Voltage(f))
      std::cout << "Voltage: " << f << std::endl;

    // Read RTC
    if (rtcDrift(tag, f))
    {
      std::cout << "Initial Clock drift: " << f << std::endl;
    }
    std::cout << "#  Checking RTC (2 second delay)" << std::endl;
    tag.SetRtc();
    std::this_thread::sleep_for(MS(2000));
    if (rtcDrift(tag, f))
    {
      std::cout << "Clock drift after setting: " << f << std::endl;
    }

    // Read state

    Status status;
    int pages;
    if (tag.GetStatus(status))
    {
      std::cout << "State: " << TagState_Name(status.state()) << std::endl;
    }

    // Read state log

    StateLog system_log;

    int next = 0;
    while (tag.GetStateLog(system_log, next))
    {
      next += system_log.states().size();

      for (auto &state : system_log.states())
      {
        std::cout << "State: " << TagState_Name(state.status().state()) << std::endl;
        std::cout <<  State_Event_Name(state.transition_reason()) << std::endl;
      }
    }

    if (status.state() != IDLE)
    {
      std::cout << "#  State not idle ! Exiting" << std::endl;
      return 0;
    }

    // Run Test

    std::cout << "#  Running test" << std::endl;
    tag.Test(RUN_ALL);
    for (int i = 0; i < 5; i++)
    {
      std::this_thread::sleep_for(MS(1000));
      tag.GetStatus(status);
      if (status.state() == IDLE)
        break;
    }
    TestResult result = status.test_status();
    std::cout << "Test Result: " << TestResult_Name(result) << std::endl;
    tag.SetRtc(); // reset rtc since test may alter
  }
  else
  {
    std::cout << "Attach failed" << std::endl;
  }

  return 0;
}
