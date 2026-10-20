/**
 * @file    tag-side-channel-check.cc
 * @brief   Hardware check for the tagcore memory side channel and keepalive.
 *
 * @details Attaches to a tag and checks, without changing tag state or
 *          writing tag memory:
 *          - the target's DBGMCU ID code reads while the tag runs;
 *          - a multi-chunk SRAM read succeeds;
 *          - unaligned and wrapping ranges return MemAccess::InvalidRange;
 *          - on STM32U3, a write over monitor_shared returns
 *            MemAccess::Protected (the guard refuses it before any write);
 *          - GetStatus and ReadMemory from two threads on one Tag never fail;
 *          - LastError() is clean after a successful RPC;
 *          - with --idle, the session survives an idle period with
 *            KeepAlive() calls, or, with --no-keepalive, the control case;
 *          - reads after Detach return MemAccess::NotAttached.
 *
 *          Prints one PASS or FAIL line per check and exits 1 on any FAIL.
 *          See host/libraries/tagcore/TODO.md. Not for shipping.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <cxxopts.hpp>
#include <tag.pb.h>
#include <tagclass.h>

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);

namespace
{

constexpr uint32_t kL4DbgmcuIdcode = 0xE0042000U;
constexpr uint32_t kU3DbgmcuIdcode = 0xE0044000U;
constexpr uint32_t kSramBase = 0x20000000U;

int failures = 0;

void report(bool ok, const std::string &what, const std::string &detail = "")
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
                detail.empty() ? "" : ": ", detail.c_str());
    std::fflush(stdout);
    if (!ok)
        failures++;
}

// A plausible STM32 DBGMCU_IDCODE has a nonzero 12-bit device ID.
bool plausible_idcode(uint32_t v)
{
    return v != 0U && v != 0xFFFFFFFFU && (v & 0xFFFU) != 0U;
}

std::string hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
}

} // namespace

int main(int argc, char **argv)
{
    Tag tag;
    UsbDev dev;
    int idle_s = 0;
    int keepalive_s = 2;
    int threads_s = 10;

    cxxopts::Options options("tag-side-channel-check",
                             "check tagcore memory access and keepalive on a live tag");
    options.add_options()
        ("idle", "idle seconds for the keepalive check (0 skips it)", cxxopts::value<int>(idle_s))
        ("interval", "seconds between KeepAlive calls", cxxopts::value<int>(keepalive_s))
        ("no-keepalive", "control: idle without KeepAlive; the session should end")
        ("threads", "seconds for the two-thread check (0 skips it)", cxxopts::value<int>(threads_s));
    if (!parse_options(argc, argv, options, tag, dev))
        return 1;
    const bool no_keepalive = options.parse(argc, argv).count("no-keepalive") > 0;

    if (!tag.Attach(dev))
    {
        std::fprintf(stderr, "Attach failed\n");
        return 1;
    }

    // Which family: try the L4 DBGMCU ID code, then the U3 one only if that
    // fails, so neither family reads the other's address unnecessarily.
    uint32_t id_l4 = 0, id_u3 = 0;
    const MemAccess r_l4 = tag.ReadMemory(kL4DbgmcuIdcode, (uint8_t *)&id_l4, 4);
    const bool is_l4 = r_l4 == MemAccess::Ok && plausible_idcode(id_l4);
    MemAccess r_u3 = MemAccess::NotAttached;
    if (!is_l4)
        r_u3 = tag.ReadMemory(kU3DbgmcuIdcode, (uint8_t *)&id_u3, 4);
    const bool is_u3 = !is_l4 && r_u3 == MemAccess::Ok && plausible_idcode(id_u3);
    report(is_u3 || is_l4, "DBGMCU ID code",
           is_u3 ? "STM32U3 " + hex(id_u3)
                 : is_l4 ? "STM32L4 " + hex(id_l4)
                         : std::string("U3 ") + MemAccessName(r_u3) + " " + hex(id_u3) +
                               ", L4 " + MemAccessName(r_l4) + " " + hex(id_l4));

    // Multi-chunk SRAM read.
    std::vector<uint8_t> sram(4096);
    auto t0 = std::chrono::steady_clock::now();
    const MemAccess r_sram = tag.ReadMemory(kSramBase, sram.data(), sram.size());
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char detail[64];
    std::snprintf(detail, sizeof(detail), "%s, %.1f ms", MemAccessName(r_sram), ms);
    report(r_sram == MemAccess::Ok, "4 KB SRAM read", detail);

    // Range checks: nothing reaches the link.
    uint32_t word = 0;
    report(tag.ReadMemory(kSramBase + 2, (uint8_t *)&word, 4) == MemAccess::InvalidRange,
           "unaligned address refused");
    report(tag.ReadMemory(kSramBase, (uint8_t *)&word, 3) == MemAccess::InvalidRange,
           "unaligned length refused");
    report(tag.ReadMemory(0xFFFFFFFCU, sram.data(), 8) == MemAccess::InvalidRange,
           "wrapping range refused");

    // Guard: refused before anything is written.
    if (is_u3)
    {
        const uint32_t zero = 0;
        const MemAccess r = tag.WriteMemory(MONITOR_SHARED_ADDR + 0x10U, (const uint8_t *)&zero, 4);
        report(r == MemAccess::Protected, "U3 monitor_shared write refused", MemAccessName(r));
    }

    // Two threads sharing one Tag.
    if (threads_s > 0)
    {
        std::atomic<bool> stop{false};
        std::atomic<int> rpc_ok{0}, rpc_fail{0}, mem_ok{0}, mem_fail{0};
        std::thread rpc([&] {
            Status status;
            while (!stop)
                (tag.GetStatus(status) ? rpc_ok : rpc_fail)++;
        });
        std::thread mem([&] {
            std::vector<uint8_t> buf(256);
            while (!stop)
                (tag.ReadMemory(kSramBase, buf.data(), buf.size()) == MemAccess::Ok ? mem_ok : mem_fail)++;
        });
        std::this_thread::sleep_for(std::chrono::seconds(threads_s));
        stop = true;
        rpc.join();
        mem.join();
        std::snprintf(detail, sizeof(detail), "%d/%d GetStatus, %d/%d reads ok",
                      rpc_ok.load(), rpc_ok + rpc_fail, mem_ok.load(), mem_ok + mem_fail);
        report(rpc_fail == 0 && mem_fail == 0 && rpc_ok > 0 && mem_ok > 0,
               "GetStatus and ReadMemory from two threads", detail);
    }

    // LastError after a successful RPC.
    Status status;
    const bool got_status = tag.GetStatus(status);
    const TagError e = tag.LastError();
    report(got_status && !e.transport_failed && e.err == Ack::OK && e.message.empty(),
           "LastError clean after GetStatus",
           got_status ? "state " + TagState_Name(status.state()) : e.message);

    // Keepalive, or the control without it.
    if (idle_s > 0)
    {
        int calls = 0, call_failures = 0;
        for (int t = 0; t < idle_s; t += keepalive_s)
        {
            std::this_thread::sleep_for(std::chrono::seconds(keepalive_s));
            if (!no_keepalive)
            {
                calls++;
                if (!tag.KeepAlive())
                    call_failures++;
            }
        }
        const bool alive = tag.GetStatus(status);
        if (no_keepalive)
            report(!alive, "control: session ended after idle",
                   alive ? "GetStatus still succeeds" : tag.LastError().message);
        else
        {
            std::snprintf(detail, sizeof(detail), "%d KeepAlive calls, %d failed", calls, call_failures);
            report(alive && call_failures == 0, "session survives idle with KeepAlive", detail);
        }
    }

    tag.Detach();
    report(tag.ReadMemory(kSramBase, (uint8_t *)&word, 4) == MemAccess::NotAttached,
           "read after Detach refused");

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
