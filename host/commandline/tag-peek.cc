/*
 * Temporary diagnostic tool: read one word of live tag memory/peripheral
 * space without resetting the tag (unlike STM32_Programmer_CLI -r32, which
 * needs connect-under-reset and can't observe a running tag). Not for
 * shipping; remove after use.
 */
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

#include <tag.pb.h>
#include <tagclass.h>
#include <cxxopts.hpp>

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);

int main(int argc, char **argv)
{
    Tag tag;
    UsbDev dev;
    std::vector<std::string> addr_strs;

    cxxopts::Options options("tag-peek", "read one or more words of live tag memory/peripheral "
                             "space in a single attach (one attach/detach disturbs the tag "
                             "less than several separate invocations)");
    options.add_options()
        ("addr", "address(es) to read, hex (e.g. 0xE000EDFC) or decimal",
         cxxopts::value<std::vector<std::string>>(addr_strs));
    options.parse_positional({"addr"});

    if (!parse_options(argc, argv, options, tag, dev))
        return 1;
    if (addr_strs.empty())
    {
        std::cerr << "usage: tag-peek <addr> [addr...]" << std::endl;
        return 1;
    }

    if (!tag.Attach(dev))
    {
        std::cerr << "Attach failed" << std::endl;
        return 1;
    }

    int rc = 0;
    for (const auto &addr_str : addr_strs)
    {
        uint32_t addr = (uint32_t)strtoul(addr_str.c_str(), nullptr, 0);
        uint32_t value = 0;
        if (!tag.ReadMemWord(addr, &value))
        {
            std::cerr << "ReadMemWord failed for " << addr_str << std::endl;
            rc = 1;
            continue;
        }
        std::printf("0x%08X: 0x%08X\n", addr, value);
    }
    return rc;
}
