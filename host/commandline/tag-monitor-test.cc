#include <stdint.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <linkadapt.h>
#include <cxxopts.hpp>

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern "C"
{
#include "monitor.h"
#include "log.h"
}

using MS = std::chrono::milliseconds;

// ARM Defined debug addresses

#define DBG_HCSR 0xE000EDF0U
#define DHCSR DBG_HCSR
#define DCRDR 0xE000EDF8U
#define DEMCR 0xE000EDFCU

#define DBGKEY (0xA05F << 16)
#define C_DEBUGEN (1 << 0)
#define C_HALT (1 << 1)
#define C_STEP (1 << 2)
#define C_MASKINTS (1 << 3)
#define S_REGRDY (1 << 16)
#define S_HALT (1 << 17)
#define S_SLEEP (1 << 18)
#define S_LOCKUP (1 << 19)
#define S_RETIRE_ST (1 << 24)
#define S_RESET_ST (1 << 25)

#define VC_CORERESET 1

// debugger register bits

#define MON_REQ (1 << 19)
#define MON_PEND (1 << 17)
#define MON_EN (1 << 16)
#define TRCENA (1 << 24)

#define RCC_CSR 0x0C000600

// Debug Handler RPC

size_t maxpacket = 0;
  // tag git sha string
uint8_t sha_str[48] = {0};
  // tag monitor version
uint32_t version = 0;
uint32_t rcc_csr = 0;

/**
 * @class   TestMonitor
 * @brief   Drives the STM32L4 DebugMonitor handshake directly over LinkAdapt.
 *
 * @details An independent implementation of the L4 attach and call sequence
 *          TagMonitor uses, for checking the monitor interface without
 *          tagcore's monitor layer. It follows the same rules:
 *          - Debug control is programmed before reset is released, because
 *            standby can power-cycle the core debug block; once the tag is
 *            running, the writes fault (ST-LINK status 0x81).
 *          - DCRDR is both request and response mailbox, so a new request is
 *            written only when MON_PEND and MON_REQ are clear, and a call
 *            waits for MON_REQ to clear before reading the result.
 *          - C_MASKINTS is never left set: it survives the halt release and
 *            stalls the firmware with every interrupt masked.
 *          STM32L4 only; an STM32U3 tag uses the shared-block transport.
 */
class TestMonitor : public LinkAdapt
{

  public: 

    /**
     * @brief   Runs one monitor operation and reads its result.
     * @param[in]  operation  DBGOP opcode.
     * @param[in]  operand    Operand, packed above the opcode.
     * @param[out] result     Receives DCRDR after the handler answers; may be
     *                        null for MONITORSTOP, which is not waited for.
     * @return  false if not attached, the monitor is still busy, a register
     *          access fails, or the handler does not answer within 2.5 s.
     */
    bool Call(uint8_t operation, int32_t operand, uint32_t *result)
    {
        static const int TIMEOUT_MS = 2500;
        uint32_t demcr = 0;

        if (!IsAttached())
        {
            log_error("monitor not attached");
            return false;
        }

        if (!ReadDebug32(DEMCR, &demcr))
        {
            log_error("DEMCR read failed");
            return false;
        }
        if (demcr & (MON_PEND | MON_REQ))
        {
            log_error("monitor busy: demcr=0x%x", demcr);
            return false;
        }

        // write the request (operand, operation), then pend DebugMon.
        // VC_CORERESET is the attachment flag the firmware reads.
        if (!WriteDebug32(DCRDR, (operand << 8) | (operation & 0xff)) ||
            !WriteDebug32(DEMCR, demcr | TRCENA | MON_EN | MON_PEND | MON_REQ | VC_CORERESET))
        {
            log_error("monitor request write failed");
            return false;
        }

        if (operation == MONITORSTOP)
        {
            // The tag may sleep as soon as it sees the stop; do not wait.
            std::this_thread::sleep_for(MS(5));
            return true;
        }

        bool answered = false;
        for (int i = 0; i < TIMEOUT_MS; i++)
        {
            if (ReadDebug32(DEMCR, &demcr) && !(demcr & MON_REQ))
            {
                answered = true;
                break;
            }
            std::this_thread::sleep_for(MS(1));
        }
        if (!answered)
        {
            log_error("monitor call timed out op=0x%x operand=%d demcr=0x%x",
                      operation, operand, demcr);
            return false;
        }

        if (result)
        {
            if (!ReadDebug32(DCRDR, result))
            {
                log_error("monitor result read failed");
                return false;
            }
            log_debug("operation = 0x%x operand = 0x%x result = 0x%x",
                      operation, operand, *result);
        }
        return true;
    }

    /**
     * @brief   Resets the tag into its monitor and starts a session.
     * @details Holds reset (LinkAdapt::Attach(true)), enables debug and arms
     *          vector catch and the monitor, releases reset, then reads the
     *          monitor version, SHA string and buffer size and sends
     *          MONITORSTART. Every step is checked.
     * @param[in] usbdev  The base to use.
     * @return  true with a session open; false, with the link closed, if any
     *          step fails.
     */
    bool Attach(UsbDev usbdev)
    {
        if (IsAttached()) {
            log_error("Already attached");
            return false;
        }

        if (!LinkAdapt::Attach(true, usbdev))
        {
            log_error("Attach failed");
            return false;
        }
        std::this_thread::sleep_for(MS(20));

        do
        {
            uint32_t demcr = 0;
            // Program debug control while the core is still held in reset.
            if (!ReadDebug32(DEMCR, &demcr) ||
                !WriteDebug32(DBG_HCSR, DBGKEY | C_DEBUGEN) ||
                !WriteDebug32(DEMCR, (demcr | TRCENA | MON_EN | VC_CORERESET) &
                                         ~(MON_REQ | MON_PEND)))
            {
                log_error("debug setup failed");
                break;
            }
            if (!AssertReset(true))
            {
                log_error("reset release failed");
                break;
            }
            std::this_thread::sleep_for(MS(50));

            uint32_t dhcsr = 0;
            if (ReadDebug32(DHCSR, &dhcsr))
            {
                log_debug("read dhcsr 0x%x", dhcsr);
                // Clear a C_MASKINTS another tool left set; only possible
                // while halted, in a write that keeps C_HALT.
                if ((dhcsr & C_MASKINTS) && (dhcsr & S_HALT))
                    WriteDebug32(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT);
            }
            if (!WriteDebug32(DBG_HCSR, DBGKEY | C_DEBUGEN))
                log_warn("halt release failed");

            if (!Call(TAG_MONITORINFO, MONITORVERSION, &version))
            {
                log_error("couldn't fetch monitor version information");
                break;
            }
            log_debug("Monitor Version 0x%x", version);

            uint32_t tmp = 0;
            if (!Call(TAG_MONITORINFO, TAGSHASTR, &tmp))
            {
                log_error("Couldn't find the address of the sha string");
                break;
            }
            if (!ReadMem32(tmp, (uint8_t *)sha_str, sizeof(sha_str)))
            {
                log_error("read_mem failed");
                break;
            }
            log_debug("Tag Hash String 0x%x, %.*s", tmp, (int)sizeof(sha_str),
                      sha_str);

            if (!Call(TAG_MONITORINFO, MONITORBUFSIZE, &tmp))
            {
                log_error("couldn't fetch monitor buffer length");
                break;
            }
            maxpacket = tmp;

            uint32_t success = 0;
            if (!Call(MONITORSTART, 0, &success) || !success)
            {
                log_error("Monitor Start failed");
                break;
            }
            return true;
        } while (0);

        LinkAdapt::Detach();
        return false;
    }

    /**
     * @brief   Ends the session with MONITORSTOP and releases the link.
     * @details As in TagMonitor, the firmware clears its own debug state on
     *          MONITORSTOP and may sleep at once, so nothing else is written.
     */
    void Detach()
    {
        if (!IsAttached())
            return;
        if (!Call(MONITORSTOP, 0, nullptr))
            log_error("Monitor Stop failed during detach");
        LinkAdapt::Detach();
    }
};

static void intHandler(int dummy)
{
    (void)dummy;
    exit(1);
}

TestMonitor monitor;


bool parse_options(int argc, char **argv, cxxopts::Options &options, UsbDev &dev)
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

        if (result.count("debug"))
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

    monitor.Available(devs);
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


int main(int argc, char **argv)
{
    UsbDev dev;

    cxxopts::Options options("tag-monitor-test", "tests the monitor interface");

    // Parse options

    if (parse_options(argc, argv, options, dev) && monitor.Attach(dev))
    {

        // catch ctl-c
        signal(SIGINT, intHandler);

        std::cout << "Monitor version 0x" << std::hex << version << std::dec
                  << ", buffer " << maxpacket << " bytes, SHA "
                  << std::string((const char *)sha_str,
                                 strnlen((const char *)sha_str, sizeof(sha_str)))
                  << std::endl;
        // End the session rather than leave it to the firmware's timeout.
        monitor.Detach();
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
