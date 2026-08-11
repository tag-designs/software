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

    if (parse_options(argc, argv, options, tag, dev) && tag.Attach(dev))
    {
        TagInfo info;

        // print floating point as  xxxxx.yy
        std::cout << std::fixed;
        std::cout.precision(2);

        // catch ctl-c
        signal(SIGINT, intHandler);

        // read tag information

        Status status;
        tag.GetStatus(status);
        bool start_attempted = false;
        bool start_failed = false;
        if (status.state() == IDLE)
        {
            Config cfg;
            tag.GetConfig(cfg);
            start_attempted = true;
            if (!tag.Start(cfg))
            {
                start_failed = true;
                std::string message = tag.DebugMessage();
                std::cout << "Start failed";
                if (!message.empty())
                {
                    std::cout << ": " << message;
                }
                std::cout << std::endl;
            }
        }
        else
        {
            std::cout << "Start skipped: tag is " << TagState_Name(status.state())
                      << std::endl;
        }

        if (!start_attempted || !start_failed)
        {
            tag.GetStatus(status);
            std::cout << "State: " << TagState_Name(status.state()) << std::endl;
        }
        else
        {
            std::cout << "Last known state: " << TagState_Name(status.state())
                      << std::endl;
        }
    }
    else
    {
        std::cout << "Attach failed" << std::endl;
    }

    return 0;
}
