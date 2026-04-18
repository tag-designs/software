#include <stdint.h>
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

// Debug Handler RPC

size_t maxpacket = 0;
  // tag git sha string
uint8_t sha_str[48] = {0};
  // tag monitor version
uint32_t version = 0;

class TestMonitor : public LinkAdapt
{

  public: 

    bool Call(uint8_t operation, int32_t operand, uint32_t *result)
    {
        static const int TIMEOUT = 500;
        uint32_t demcr, dhcsr;
        int err;
        int i;

        if (!IsAttached())
        {
            log_error("monitor not attached");
            return false;
        }

        /*
        if (!ReadDebug32(DHCSR, &dhcsr))
        {
            log_error("read reg failed %d\n",dhcsr);
            return false;
        } else {
            log_debug("read dhcsr 0x%x\n",dhcsr);
        }
            */

        // write debug request data (operand,operation)

        if (!WriteDebug32(DCRDR, (operand << 8) | (operation & 0xff)))
        {
            log_error("write reg failed");
            return false;
        }

        // write debug interrupt request -- Set MON_PEND, MON_REQ in DEMCR
        // VC_CORERESET is used as attachment flag to embedded app.
        // This also causes core to halt in reset vector after reset
        // Read/Modify/Write DEMCR

        if (!ReadDebug32(DEMCR, &demcr))
        {
            log_error("read reg failed %d\n",demcr);
            return false;
        }
/*
        if (demcr & (MON_PEND | MON_REQ)){
            log_error("monitor pending bit already set");
            return false;
        }
*/

        if (!WriteDebug32(DEMCR, (demcr | MON_PEND | MON_EN)))// | MON_REQ)))// | VC_CORERESET)))
        {
            log_error("write reg failed");
            return false;
        }

        // wait for result by polling MON_REQ bit

        for (i = 0; i < TIMEOUT*5; i++)
        {
            if (!ReadDebug32(DEMCR, &demcr))
            log_error("read_mem failed\n");
            else if (demcr & MON_REQ) //(!(demcr & MON_REQ))
            break;
            std::this_thread::sleep_for(MS(1));
        }

        // check for timeout

        if (i == TIMEOUT*5)
        {
            log_error("monitor call timed out\n");
            return false;
        }
/*
        if (demcr & MON_REQ){
            log_error("monitor pending bit still set");
            return false;
        }
            */


        // read result value if pointer to destination is not NULL

        if (result)
        {
            for (i = 0; i < 5; i++)
            {
            if (ReadDebug32(DCRDR, result))
                break;
            log_error("read_mem failed");
            std::this_thread::sleep_for(MS(1));
            }

            // check for a timeout

            if (i == 5)
            {
            log_error("monitor call timed out\n");
            return false;
            }
        }
        log_debug("operation = 0x%x operand = 0x%x result = 0x%x\n",operation, operand, *result);
        return true;
    }

    bool Attach(UsbDev usbdev)
    {
        int err;
        uint32_t demcr;
        uint32_t debugval;
        uint32_t resetvector;

        if (IsAttached()) {
            log_error("Already attached");
            return false;
        }

        do
        {
            // connect to stlink

            if (!LinkAdapt::Attach(usbdev))
            {
            log_error("Attach failed");
            return false;
            }

            // read control register

            if (!ReadDebug32(DEMCR, &demcr))
            {
            log_error("Read failed");
            LinkAdapt::Detach();
            break;
            }

            // Write debug magic key and
            // Set VC_CORERESET (halt in reset)
            // Set MON_EN (enable handler); clear request bits

            if (!(WriteDebug32(DBG_HCSR, DBGKEY | 1) &&
                WriteDebug32(DEMCR,
                    ((demcr | MON_EN) & // | VC_CORERESET) & 
                        ~MON_REQ & ~MON_PEND))))
            {

            log_error("Monitor attach failed: write returned");
            LinkAdapt::Detach();
            break;
            }

            // remove reset

            if (!AssertReset(true))
            {
            log_error("Monitor attach failed: Reset assert");
            LinkAdapt::Detach();
            break;
            }

            // give time for reset to complete

            std::this_thread::sleep_for(MS(5));

            // clear the halt

            WriteDebug32(DBG_HCSR, DBGKEY);// | 1);

            // Call monitor to get pointer to information block

            if (!Call(TAG_MONITORINFO, MONITORVERSION, &version))
            {
            log_error("couldn't fetch monitor version information");
            LinkAdapt::Detach();
            break;
            }

            log_debug("Monitor Version 0x%x", version);

            uint32_t tmp = 0;

            if (!Call(TAG_MONITORINFO, TAGSHASTR, &tmp))
            {
            log_error("Couldn't find the address of the sha string");
            LinkAdapt::Detach();
            }
            else
            {
            if (!ReadMem32(tmp, (uint8_t *)sha_str, sizeof(sha_str)))
            {
                log_error("read_mem failed\n");
                LinkAdapt::Detach();
                return false;
            }
            }

            log_debug("Tag Hash String 0x%x, %s\n", tmp, sha_str);

            if (!Call(TAG_MONITORINFO, MONITORBUFSIZE, &tmp))
            {
            log_error("couldn't fetch monitor buffer length");
            LinkAdapt::Detach();
            break;
            }

            maxpacket = tmp;


            uint32_t success;
            if (!Call(MONITORSTART, 0, &success) || !success)
            {
            log_error("Monitor Start failed");
            LinkAdapt::Detach();
            break;
            }

            return true;
        } while (0);

        // try detach to clean up

        Detach();
        return false;
    }

    void Detach()
    {
        uint32_t demcr;

        if (!IsAttached())
            return;
        maxpacket = 0;

        memset(sha_str,0, sizeof(sha_str));
        version = 0;
        // Detach call to mon->handler

        Call(MONITORSTOP, 0, 0);
        // Clear debug register bits
        ReadDebug32(DEMCR, &demcr);
        WriteDebug32(DEMCR, (demcr & ~(VC_CORERESET | MON_PEND | MON_REQ | MON_EN)));
        // release usb
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
        
    }
    else
    {
        std::cout << "Attach failed" << std::endl;
    }

    return 0;
}
