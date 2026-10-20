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
 *          Three opt-in checks change tag memory or exercise failures, and
 *          need the operator's agreement first:
 *          - --write-test <addr> writes two patterns to one SRAM word, reads
 *            each back, then restores and re-reads the original value. The
 *            address must be word aligned, in SRAM and outside
 *            monitor_shared on both families; it is never forced.
 *          - --start-running sends Start to a tag already RUNNING or
 *            HIBERNATING and expects Ack::PERM without a transport failure.
 *            It refuses to send Start to a tag in any other state.
 *          - --unplug <s> polls GetStatus for up to @p s seconds while the
 *            operator unplugs the base, and expects the failure to be
 *            reported as a transport failure. It ends the run.
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
/// Upper bound for --write-test addresses: the largest SRAM of the targets
/// (STM32U375, 256 KB). The operator still picks the word from the map file.
constexpr uint32_t kSramLimit = 0x20040000U;

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

/**
 * @brief   Polls GetStatus until the tag reports a definite state.
 * @details Attach connects under reset, so the first status can be
 *          STATE_UNSPECIFIED while the tag boots.
 * @param[in]  tag     Attached tag.
 * @param[out] status  Last status read.
 * @return  true once a state other than STATE_UNSPECIFIED is read; false on
 *          an RPC failure or after about 10 s.
 */
bool settled_status(Tag &tag, Status &status)
{
    for (int i = 0; i < 10; i++)
    {
        if (!tag.GetStatus(status))
            return false;
        if (status.state() != STATE_UNSPECIFIED)
            return true;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/**
 * @brief   Writes patterns to one SRAM word, reads them back, restores it.
 * @param[in] tag   Attached tag.
 * @param[in] addr  Word address agreed with the operator from the map file.
 * @return  Nothing; reports one PASS or FAIL line per step.
 */
void write_test(Tag &tag, uint32_t addr)
{
    if ((addr & 3U) != 0U || addr < MONITOR_SHARED_ADDR + MONITOR_SHARED_SIZE ||
        addr >= kSramLimit)
    {
        report(false, "write test", hex(addr) +
               " is unaligned, inside monitor_shared or outside SRAM; nothing written");
        return;
    }
    uint32_t original = 0;
    MemAccess r = tag.ReadMemory(addr, (uint8_t *)&original, 4);
    report(r == MemAccess::Ok, "write test: read original at " + hex(addr),
           std::string(MemAccessName(r)) + " " + hex(original));
    if (r != MemAccess::Ok)
        return;
    for (uint32_t pattern : {0xA5C3F00FU, 0x5A3C0FF0U})
    {
        uint32_t back = 0;
        r = tag.WriteMemory(addr, (const uint8_t *)&pattern, 4);
        const MemAccess rb = r == MemAccess::Ok
                                 ? tag.ReadMemory(addr, (uint8_t *)&back, 4)
                                 : r;
        report(r == MemAccess::Ok && rb == MemAccess::Ok && back == pattern,
               "write test: " + hex(pattern) + " reads back",
               std::string(MemAccessName(r)) + "/" + MemAccessName(rb) + " " + hex(back));
    }
    uint32_t restored = ~original;
    r = tag.WriteMemory(addr, (const uint8_t *)&original, 4);
    const MemAccess rb = r == MemAccess::Ok
                             ? tag.ReadMemory(addr, (uint8_t *)&restored, 4)
                             : r;
    report(r == MemAccess::Ok && rb == MemAccess::Ok && restored == original,
           "write test: original restored",
           std::string(MemAccessName(r)) + "/" + MemAccessName(rb) + " " + hex(restored));
}

/**
 * @brief   Sends Start to a running tag and checks LastError reports PERM.
 * @details Refuses to send Start unless the tag reports RUNNING or
 *          HIBERNATING, so it can never start an idle tag.
 * @param[in] tag  Attached tag.
 */
void start_running_test(Tag &tag)
{
    Status status;
    if (!settled_status(tag, status))
    {
        report(false, "Start on running tag", "no definite state: " + tag.LastError().message);
        return;
    }
    if (status.state() != RUNNING && status.state() != HIBERNATING)
    {
        report(false, "Start on running tag",
               "tag is " + TagState_Name(status.state()) + "; Start not sent");
        return;
    }
    Config cfg;
    if (!tag.GetConfig(cfg))
    {
        report(false, "Start on running tag", "GetConfig failed: " + tag.LastError().message);
        return;
    }
    const bool started = tag.Start(cfg);
    const TagError e = tag.LastError();
    report(!started && e.err == Ack::PERM && !e.transport_failed,
           "Start on running tag gives PERM without transport failure",
           std::string(started ? "Start succeeded" : "Start refused") + ", err " +
               Ack::Err_Name(e.err) + ", transport_failed " +
               (e.transport_failed ? "true" : "false") +
               (e.message.empty() ? "" : ", \"" + e.message + "\""));
}

/**
 * @brief   Polls GetStatus while the operator unplugs the base.
 * @param[in] tag        Attached tag.
 * @param[in] seconds    How long to wait for the unplug.
 */
void unplug_test(Tag &tag, int seconds)
{
    std::fprintf(stderr, "Unplug the base now (waiting up to %d s)...\n", seconds);
    Status status;
    int ok = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end)
    {
        if (!tag.GetStatus(status))
        {
            const TagError e = tag.LastError();
            char detail[96];
            std::snprintf(detail, sizeof(detail), "after %d good calls, err %s, ", ok,
                          Ack::Err_Name(e.err).c_str());
            report(e.transport_failed, "unplug reported as transport failure",
                   detail + ("transport_failed " + std::string(e.transport_failed ? "true" : "false")) +
                       (e.message.empty() ? "" : ", \"" + e.message + "\""));
            return;
        }
        ok++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    report(false, "unplug reported as transport failure", "no failure within the wait");
}

} // namespace

int main(int argc, char **argv)
{
    Tag tag;
    UsbDev dev;
    int idle_s = 0;
    int keepalive_s = 2;
    int threads_s = 10;
    int unplug_s = 0;
    std::string write_addr;

    cxxopts::Options options("tag-side-channel-check",
                             "check tagcore memory access and keepalive on a live tag");
    options.add_options()
        ("idle", "idle seconds for the keepalive check (0 skips it)", cxxopts::value<int>(idle_s))
        ("interval", "seconds between KeepAlive calls", cxxopts::value<int>(keepalive_s))
        ("no-keepalive", "control: idle without KeepAlive; the session should end")
        ("threads", "seconds for the two-thread check (0 skips it)", cxxopts::value<int>(threads_s))
        ("write-test", "write, read back and restore one SRAM word (changes tag memory)",
         cxxopts::value<std::string>(write_addr))
        ("start-running", "send Start to a running tag, expect PERM (asks the tag to start)")
        ("unplug", "seconds to wait for the base to be unplugged during GetStatus calls",
         cxxopts::value<int>(unplug_s));
    if (!parse_options(argc, argv, options, tag, dev))
        return 1;
    const auto parsed = options.parse(argc, argv);
    const bool no_keepalive = parsed.count("no-keepalive") > 0;
    const bool start_running = parsed.count("start-running") > 0;

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

    if (!write_addr.empty())
    {
        char *end = nullptr;
        const unsigned long addr = std::strtoul(write_addr.c_str(), &end, 0);
        if (end == write_addr.c_str() || *end != '\0' || addr > 0xFFFFFFFFUL)
            report(false, "write test", "cannot parse address " + write_addr);
        else
            write_test(tag, (uint32_t)addr);
    }
    if (start_running)
        start_running_test(tag);
    if (unplug_s > 0)
    {
        unplug_test(tag, unplug_s);
        std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                    failures == 1 ? "" : "s");
        return failures ? 1 : 0;
    }

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
