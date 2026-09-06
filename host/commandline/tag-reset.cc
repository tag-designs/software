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
    int settle_timeout_s = 10;

    cxxopts::Options options("tag-reset",
                             "stop, erase, and return a tag to the idle state");
    options.add_options()
        ("set-rtc", "Synchronize the tag clock from the host once idle",
         cxxopts::value<bool>(set_rtc)->default_value("false"))
        ("reset-timeout", "Seconds to wait for the erase to finish",
         cxxopts::value<int>(reset_timeout_s)->default_value("180"))
        ("settle-timeout",
         "Seconds to wait for the tag to report a definite state after attach. "
         "Attach connects under reset, so the first status can legitimately be "
         "STATE_UNSPECIFIED while the tag boots",
         cxxopts::value<int>(settle_timeout_s)->default_value("10"));

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
        /*
         * A failed status read must not be mistaken for a tag that is already
         * idle. Status default-constructs with state() == STATE_UNSPECIFIED,
         * which matches neither RUNNING/HIBERNATING nor FINISHED/ABORTED, so
         * ignoring this return value made the tool skip both the stop and the
         * erase, still set the clock, print "Final state: STATE_UNSPECIFIED"
         * and exit 0 -- reporting success for a reset that never happened.
         * Seen in roughly one storm clock-cycle in twenty.
         */
        /*
         * Attach connects under reset, so the tag is still booting and its
         * first status can report STATE_UNSPECIFIED -- pState->state is zero
         * until the state machine restores it. That is a tag that has not
         * settled yet, not a tag in an unknown state, and treating it as the
         * latter skipped the erase and reported a reset that never happened.
         * tag-start already waits for this; tag-reset did not, which is the
         * "tag is STATE_UNSPECIFIED after reset" seen in roughly one storm
         * clock-cycle in twenty, and the neighbouring "SetRtc failed", since
         * the firmware rejects a clock write issued before the tag settles.
         */
        /* A second between tries: each poll is a request the tag must
           service while it is still coming up, so spacing them out competes
           less with the boot being waited on. */
        const int settle_ms = 1000;
        const int settle_tries =
            (settle_timeout_s * 1000 + settle_ms - 1) / settle_ms;
        bool settled = false;
        for (int i = 0; i < settle_tries; i++)
        {
            if (!tag.GetStatus(status))
            {
                std::cerr << "GetStatus failed: " << tag.DebugMessage()
                          << std::endl;
                return 1;
            }
            if (status.state() != STATE_UNSPECIFIED)
            {
                settled = true;
                break;
            }
            std::this_thread::sleep_for(MS(settle_ms));
        }
        if (!settled)
        {
            std::cerr << "tag still reports STATE_UNSPECIFIED after "
                      << settle_timeout_s << " s; it is not merely settling"
                      << std::endl;
            return 1;
        }
        if (!status.debug_message().empty()){
            std::cerr << status.debug_message();
        }
        std::cout << status.DebugString() << std::endl;
        if (status.state() == RUNNING || status.state() == HIBERNATING)
        {
            if (!tag.Stop())
            {
                std::cerr << "Stop failed: " << tag.DebugMessage()
                          << std::endl;
                return 1;
            }
            std::cout << "State: " << TagState_Name(status.state()) << std::endl;
            if (!tag.GetStatus(status))
            {
                std::cerr << "GetStatus after stop failed: "
                          << tag.DebugMessage() << std::endl;
                return 1;
            }
            if (!status.debug_message().empty()){
                std::cerr << status.debug_message();
            }
        }

        std::cout << "State: " << TagState_Name(status.state()) << std::endl;
        if (status.state() == FINISHED || status.state() == ABORTED)
        {
            if (!tag.Erase())
            {
                std::cerr << "Erase failed: " << tag.DebugMessage()
                          << std::endl;
                return 1;
            }

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
                /* The tag says why: the firmware answers a failed clock
                   write with Ack_Err_NXIO and "RTC sync failed while writing
                   tag clock". Printing only "SetRtc failed" discarded that and
                   left an intermittent RV-3028 write indistinguishable from a
                   refusal on state. */
                std::cerr << "SetRtc failed: " << tag.DebugMessage()
                          << std::endl;
                return 1;
            }
        }

        std::cout << "Final state: " << TagState_Name(status.state()) << std::endl;
    }
    else
    {
        std::cerr << "Attach failed" << std::endl;
        /* Exiting 0 here reported success from a tool that
           never reached the tag, which a script cannot tell
           apart from a completed operation. */
        return 1;
    }

    return 0;
}
