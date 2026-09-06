#include <stdint.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <tag.pb.h>
#include <tagclass.h>
#include <cxxopts.hpp>

extern "C"
{
#include "log.h"
}

extern bool parse_options(int argc, char **argv,
                          cxxopts::Options &options,
                          Tag &tag,
                          UsbDev &dev);

int main(int argc, char **argv)
{
    using MS = std::chrono::milliseconds;
    UsbDev dev;
    Tag tag;
    Status status;
    int64_t millis;

    cxxopts::Options options("tag-test-example",
                             "sets the RTC");

    if (parse_options(argc, argv, options, tag, dev) &&
        tag.Attach(dev))
    {
        if (!tag.SetRtc())
        {
            std::cerr << "SetRtc failed: " << tag.DebugMessage() << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(MS(2000));
        /* millis() defaults to 0, which would report a clock error of about
           -1.8e9 seconds as though it were a measurement. */
        if (!tag.GetStatus(status))
        {
            std::cerr << "GetStatus failed: " << tag.DebugMessage()
                      << std::endl;
            return 1;
        }
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::time_point_cast<MS>(now);

        // Compute error

        std::cout << "Clock Error: " << (status.millis() - ts.time_since_epoch().count()) / 1000.0 << std::endl;
    }
}
