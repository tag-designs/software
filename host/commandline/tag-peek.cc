/*
 * Diagnostic tool: read words of live tag memory/peripheral space without
 * resetting the tag (unlike STM32_Programmer_CLI -r32, which needs
 * connect-under-reset and can't observe a running tag). Uses
 * Tag::ReadMemory(), so addresses must be word aligned. Not for shipping.
 */
#include <cerrno>
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
        /* strtoul() returns 0 for text it cannot parse, so a typo read
           address 0 and printed it as though it had been asked for. Require
           the whole argument to be a number that fits in 32 bits. */
        char *end = nullptr;
        errno = 0;
        const unsigned long long parsed =
            std::strtoull(addr_str.c_str(), &end, 0);
        if (addr_str.empty() || end == addr_str.c_str() || *end != '\0' ||
            errno == ERANGE || addr_str[0] == '-' || parsed > 0xFFFFFFFFull)
        {
            std::cerr << "not an address: " << addr_str << std::endl;
            rc = 1;
            continue;
        }
        const uint32_t addr = static_cast<uint32_t>(parsed);
        uint32_t value = 0;
        const MemAccess result = tag.ReadMemory(addr, (uint8_t *)&value, sizeof(value));
        if (result != MemAccess::Ok)
        {
            std::cerr << "read failed for " << addr_str << ": "
                      << MemAccessName(result) << std::endl;
            rc = 1;
            continue;
        }
        std::printf("0x%08X: 0x%08X\n", addr, value);
    }
    return rc;
}
