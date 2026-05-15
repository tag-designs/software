#include <stdint.h>
#include <string>
#include <vector>
#include <cxxopts.hpp>

#include <tag.pb.h>
#include <tagclass.h>

extern "C"
{
#include "log.h"
}

bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev)
{
    std::string device;
    std::vector<UsbDev> devs;

    options.add_options()("d,debug", "Set log level to DEBUG")("b,base", "Select bus:device", cxxopts::value<std::string>())("h,help", "Print usage"); // a bool parameter

    try
    {
        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        if (result.count("base"))
        {
            device = result["base"].as<std::string>();
        }

        if (result["debug"].as<bool>())
        {
            log_set_level(LOG_DEBUG);
        }
        else
        {
            log_set_level(LOG_ERROR);
        }
    }
    catch (const cxxopts::OptionException &e)
    {
        std::cout << options.help() << std::endl;
        std::cerr << "error parsing options: " << e.what() << std::endl;
        exit(1);
    }

    // Match device

    tag.Available(devs);
    int devnum = 0;

    if ((devs.size() > 1) || device.size())
    {
        unsigned int bus, address;
        if (sscanf(device.c_str(), "%u:%u", &bus, &address) != 2)
        {
            std::cout << options.help() << std::endl;
            exit(1);
        }
        while (devnum < devs.size())
        {
            if ((devs[devnum].bus == bus) &&
                (devs[devnum].address == address))
            {
                break;
            }
            devnum++;
        }
    }

    if (devnum >= devs.size())
    {
        std::cerr << "No matching device " << device << std::endl;
        exit(1);
    }

    dev = devs[devnum];
    return true;
}