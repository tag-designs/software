#include <algorithm>
#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <fstream>
#include <iterator>

#include <google/protobuf/util/json_util.h>
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

/**
 * @brief Load a Config from a protobuf-JSON file.
 *
 * @details The default configurations under embedded/proto-c/<tag>-proto-c/
 *          are partial: they carry tag_type, start_delay and sensor settings but
 *          no schedule. Pass merge=true to overlay such a file onto the
 *          configuration already stored on the tag; pass false to program
 *          exactly what the file specifies, which is what a reproducible
 *          experiment wants.
 *
 * @param[in]     path  JSON file to read.
 * @param[in,out] cfg   On entry the tag's current configuration; on success the
 *                      configuration to program.
 * @param[in]     merge true to overlay onto @p cfg, false to replace it.
 * @return true on success; false with a message on stderr if the file cannot be
 *         read or the JSON does not parse as a Config.
 */
static bool loadConfigJson(const std::string &path, Config &cfg, bool merge)
{
    std::ifstream fin(path);
    if (!fin)
    {
        std::cerr << "Cannot open config file: " << path << std::endl;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(fin)),
                     std::istreambuf_iterator<char>());
    fin.close();

    Config parsed;
    google::protobuf::util::JsonParseOptions parse_options;
    auto status = google::protobuf::util::JsonStringToMessage(text, &parsed,
                                                             parse_options);
    if (!status.ok())
    {
        std::cerr << "Config JSON did not parse: " << status.ToString()
                  << std::endl;
        return false;
    }

    if (merge)
    {
        // MergeFrom leaves proto3 scalar fields that are zero in the source
        // untouched, so a merged file cannot clear a value back to zero. Use
        // --start-now for that, or omit --merge.
        cfg.MergeFrom(parsed);
    }
    else
    {
        cfg = parsed;
    }
    return true;
}

int main(int argc, char **argv)
{
    Tag tag;
    UsbDev dev;

    std::string config_path;
    bool merge_config = false;
    bool set_rtc = false;
    bool start_now = false;
    int start_timeout_s = 10;
    int settle_timeout_s = 10;

    cxxopts::Options options("tag-start",
                             "start a configured tag and print the resulting status");
    options.add_options()
        ("c,config", "Program this protobuf-JSON configuration before starting",
         cxxopts::value<std::string>(config_path))
        ("merge", "Overlay --config onto the tag's stored configuration rather "
                  "than replacing it",
         cxxopts::value<bool>(merge_config)->default_value("false"))
        ("set-rtc", "Synchronize the tag clock from the host before starting",
         cxxopts::value<bool>(set_rtc)->default_value("false"))
        ("start-now", "Force start_delay to zero so collection begins immediately",
         cxxopts::value<bool>(start_now)->default_value("false"))
        ("start-timeout",
         "Seconds to wait for the tag to leave IDLE after the start is accepted. "
         "Start is posted, not performed: the state machine acts on it later, so "
         "a status read straight afterwards can still say IDLE",
         cxxopts::value<int>(start_timeout_s)->default_value("10"))
        ("settle-timeout",
         "Seconds to wait after attach for the tag to report a definite state. "
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

        // Read status before synchronizing the clock. Attach connects under
        // reset, so the tag needs a round trip to settle; the firmware accepts
        // a set_rtc request only in IDLE (monitor.c, Req_set_rtc_tag), and one
        // issued as the first request after attach is rejected.
        Status status;
        /*
         * Checked, because Status default-constructs to STATE_UNSPECIFIED and
         * every decision below turns on state(). A discarded failure here
         * silently becomes "the tag is not IDLE", which reports a refusal the
         * tag never made.
         *
         * Polled, because the first status after a connect-under-reset can
         * legitimately be STATE_UNSPECIFIED while the tag boots. Taking that
         * as "not IDLE" skipped the start and then reported the tag IDLE with
         * a success exit -- one storm set in three. tag-reset and tag-stop do
         * the same wait.
         */
        {
            const int settle_ms = 1000;
            const int settle_tries =
                std::max(1, (settle_timeout_s * 1000 + settle_ms - 1) / settle_ms);
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
                std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
            }
            if (!settled)
            {
                std::cerr << "tag still reports STATE_UNSPECIFIED after "
                          << settle_timeout_s << " s; it is not merely settling"
                          << std::endl;
                return 1;
            }
        }

        if (set_rtc)
        {
            if (status.state() != IDLE)
            {
                std::cerr << "SetRtc skipped: tag is "
                          << TagState_Name(status.state())
                          << ", the tag accepts a clock sync only when IDLE"
                          << std::endl;
                return 1;
            }
            if (!tag.SetRtc())
            {
                std::string why = tag.DebugMessage();
                std::cerr << "SetRtc failed"
                          << (why.empty() ? "" : ": " + why) << std::endl;
                return 1;
            }
            std::cout << "RTC synchronized" << std::endl;
        }

        bool start_attempted = false;
        bool start_failed = false;
        if (status.state() == IDLE)
        {
            Config cfg;
            /* Starting from a default-constructed config would run the tag
               with settings nobody chose. */
            if (!tag.GetConfig(cfg))
            {
                std::cerr << "GetConfig failed: " << tag.DebugMessage()
                          << std::endl;
                return 1;
            }
            if (!config_path.empty() &&
                !loadConfigJson(config_path, cfg, merge_config))
            {
                return 1;
            }
            if (start_now)
            {
                cfg.set_start_delay(0);
            }
            std::cout << "Starting with configuration:" << std::endl
                      << cfg.DebugString();
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
        else if (status.state() == RUNNING || status.state() == CONFIGURED)
        {
            std::cout << "Start skipped: tag is already "
                      << TagState_Name(status.state()) << std::endl;
        }
        else
        {
            /* A skipped start is not a success for a tool whose job is to start. */
            std::cerr << "Start skipped: tag is " << TagState_Name(status.state())
                      << "; a start is accepted only in IDLE" << std::endl;
            std::cout << "State: " << TagState_Name(status.state()) << std::endl;
            return 1;
        }

        if (!start_attempted || !start_failed)
        {
            /*
             * Start() returns when the request is accepted; the monitor
             * handler only posts MON_WORK_START and the state machine acts
             * on it later. One status read straight afterwards can still say
             * IDLE, which tag_attach_storm.py then reports as "tag is IDLE
             * after start" -- seen on one storm set in three. Poll, a second
             * apart, until the tag has left IDLE, as tag-stop polls for
             * FINISHED.
             */
            const int poll_ms = 1000;
            const int tries = start_attempted
                ? std::max(1, (start_timeout_s * 1000 + poll_ms - 1) / poll_ms)
                : 1;
            bool read_ok = false;
            for (int i = 0; i < tries; i++)
            {
                read_ok = tag.GetStatus(status);
                if (!read_ok)
                {
                    break;
                }
                if (!start_attempted || status.state() != IDLE)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            }
            if (!read_ok)
            {
                /* Report the last state actually read, not a zeroed one. */
                std::cerr << "GetStatus failed: " << tag.DebugMessage()
                          << std::endl;
                std::cout << "Last known state: "
                          << TagState_Name(status.state()) << std::endl;
                return 1;
            }
            if (start_attempted && status.state() == IDLE)
            {
                std::cerr << "Tag did not leave IDLE within " << start_timeout_s
                          << " s of the start being accepted" << std::endl;
                std::cout << "State: " << TagState_Name(status.state())
                          << std::endl;
                return 1;
            }
            std::cout << "State: " << TagState_Name(status.state())
                      << std::endl;
            /*
             * Leaving IDLE is not the same as having started. A start that
             * fails in device setup goes IDLE -> ABORTED, and a status read
             * that lands there must not be reported as success.
             */
            if (start_attempted && status.state() != RUNNING &&
                status.state() != CONFIGURED)
            {
                std::cerr << "Start did not lead to RUNNING or CONFIGURED: "
                          << TagState_Name(status.state()) << std::endl;
                return 1;
            }
        }
        else
        {
            std::cout << "Last known state: " << TagState_Name(status.state())
                      << std::endl;
        }
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
