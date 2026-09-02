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

    bool set_rtc = false;
    int reset_timeout_s = 180;

    cxxopts::Options options("tag-reset",
                             "stop, erase, and return a tag to the idle state");
    options.add_options()
        ("set-rtc", "Synchronize the tag clock from the host once idle",
         cxxopts::value<bool>(set_rtc)->default_value("false"))
        ("reset-timeout", "Seconds to wait for the erase to finish",
         cxxopts::value<int>(reset_timeout_s)->default_value("180"));

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
        if (!status.debug_message().empty()){
            std::cerr << status.debug_message();
        }
        std::cout << status.DebugString() << std::endl;
        if (status.state() == RUNNING || status.state() == HIBERNATING)
        {
            tag.Stop();
            std::cout << "State: " << TagState_Name(status.state()) << std::endl;
            tag.GetStatus(status);
            if (!status.debug_message().empty()){
                std::cerr << status.debug_message();
            }
        }

        std::cout << "State: " << TagState_Name(status.state()) << std::endl;
        if (status.state() == FINISHED || status.state() == ABORTED)
        {
            tag.Erase();

            /*
             * Poll for completion rather than guessing at a duration.
             * Tag::Erase() only posts the work; the state machine then sweeps
             * external storage in batches, returning SLEEP between them, and
             * the sweep length depends on how much data is actually present
             * because it probes the device rather than trusting a stored
             * cursor. The previous fixed two-second sleep was therefore
             * sometimes short -- reporting a state read mid-reset, which made
             * scripted use nondeterministic -- and otherwise wasteful.
             */
            const int poll_ms = 250;
            const int timeout_ms = reset_timeout_s * 1000;
            int waited_ms = 0;
            bool reached_idle = false;

            while (waited_ms < timeout_ms)
            {
                std::this_thread::sleep_for(MS(poll_ms));
                waited_ms += poll_ms;
                if (!tag.GetStatus(status))
                {
                    continue;
                }
                if (status.state() == IDLE)
                {
                    reached_idle = true;
                    break;
                }
            }

            std::cout << "State: " << TagState_Name(status.state())
                      << " after " << (waited_ms / 1000.0) << " s" << std::endl;
            if (!status.debug_message().empty()){
                std::cerr << status.debug_message();
            }
            if (!reached_idle)
            {
                std::cerr << "Reset did not reach IDLE within "
                          << reset_timeout_s << " s; last state "
                          << TagState_Name(status.state()) << std::endl;
                return 1;
            }
        }

        if (set_rtc)
        {
            if (tag.SetRtc())
            {
                std::cout << "RTC synchronized" << std::endl;
            }
            else
            {
                std::cerr << "SetRtc failed" << std::endl;
                return 1;
            }
        }

        std::cout << "Final state: " << TagState_Name(status.state()) << std::endl;
    }
    else
    {
        std::cout << "Attach failed" << std::endl;
    }

    return 0;
}
