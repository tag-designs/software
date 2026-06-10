#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <thread>

#include <iostream>
#include <vector>
#include <mutex>

using namespace std;
using MS = std::chrono::milliseconds;

extern "C"
{
#include "monitor.h"
#include "log.h"
}

#include <tag.pb.h>
#include "tagmonitor.h"

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


// TagMonitor Functions

TagMonitor::TagMonitor()
{
 
} 

TagMonitor::~TagMonitor()
{
  Detach();
}

bool TagMonitor::Voltage(float &voltage){
  return LinkAdapt::Voltage(voltage);
}

void TagMonitor::ResetMonitorStats()
{
  monitor_stats = TagMonitorStats();
}

TagMonitorStats TagMonitor::GetMonitorStats() const
{
  return monitor_stats;
}

#if TAGCORE_ENABLE_INSTRUMENTATION
static uint64_t monitor_elapsed_ns(std::chrono::steady_clock::time_point start)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}
#endif

// Debug Handler RPC

bool TagMonitor::Call(uint8_t operation, int32_t operand, uint32_t *result)
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

  if (demcr & (MON_PEND | MON_REQ)){
    log_error("monitor pending bit already set");
    return false;
  }

  if (!WriteDebug32(DEMCR, (demcr | MON_PEND | MON_REQ | VC_CORERESET)))
  {
    log_error("write reg failed");
    return false;
  }

  // wait for result by polling MON_REQ bit

  for (i = 0; i < TIMEOUT*5; i++)
  {
    if (!ReadDebug32(DEMCR, &demcr))
      log_error("read_mem failed\n");
    else if (!(demcr & MON_REQ))
      break;
    std::this_thread::sleep_for(MS(1));
  }

  // check for timeout

  if (i == TIMEOUT*5)
  {
    log_error("monitor call timed out\n");
    return false;
  }

  if (demcr & MON_REQ){
    log_error("monitor pending bit still set");
    return false;
  }


  // read result value if pointer to destination is not NULL

  if (result)
  {
    for (i = 0; i < TIMEOUT; i++)
    {
      if (ReadDebug32(DCRDR, result))
        break;
      log_error("read_mem failed");
      std::this_thread::sleep_for(MS(1));
    }

    // check for a timeout

    if (i == TIMEOUT-1)
    {
      log_error("monitor call timed out\n");
      return false;
    }
  }
  //log_debug("operation = 0x%x operand = 0x%x result = 0x%x\n",operation, operand, *result);
  return true;
}

bool TagMonitor::Attach(UsbDev usbdev)
{
  int err;
  uint32_t demcr;
  uint32_t debugval;
  uint32_t resetvector;

  if (IsAttached()) {
    log_error("Already attached");
    return false;
  }

  call_buf = 0;

  do
  {
    // connect to stlink

    if (!LinkAdapt::Attach(usbdev))
    {
      log_error("Attach failed");
      return false;
    }

    std::this_thread::sleep_for(MS(20));

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
              ((demcr | MON_EN | VC_CORERESET) & 
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

    std::this_thread::sleep_for(MS(50));

    // clear the halt

    WriteDebug32(DBG_HCSR, DBGKEY | 1);

    // Call monitor to get pointer to information block

    if (!Call(TAG_MONITORINFO, MONITORVERSION, &version))
    {
      log_error("couldn't fetch monitor version information");
      LinkAdapt::Detach();
      break;
    }

    log_debug("Monitor Version 0x%x", version);

    if (!Call(TAG_MONITORINFO, MONITORBUF, &call_buf))
    {
      log_error("couldn't fetch monitor buffer location");
      LinkAdapt::Detach();
      break;
    }

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

    if (maxpacket > sizeof(rpcbuf)) {
      log_error("RPC buffer is too small");
      LinkAdapt::Detach();
      break;
    }

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

void TagMonitor::Detach()
{
  uint32_t demcr;

  if (!IsAttached())
    return;
  maxpacket = 0;
  call_buf = 0;
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

// RPC call for monitor

bool TagMonitor::Rpc(Req &req, Ack &ack)
{
  uint32_t retval;
  int err = 0;
  uint16_t len;
#if TAGCORE_ENABLE_INSTRUMENTATION
  auto rpc_start = std::chrono::steady_clock::now();
#endif

  ack.Clear();

  if (!IsAttached()) {
    log_error("Monitor not attached");
    return false;
  }

  size_t size = req.ByteSizeLong();
  if (size > sizeof(rpcbuf)) {
    log_error("Request message too big");
    return false;
  }
#if TAGCORE_ENABLE_INSTRUMENTATION
  auto step_start = std::chrono::steady_clock::now();
#endif
  req.SerializeToArray(rpcbuf, sizeof(rpcbuf));
#if TAGCORE_ENABLE_INSTRUMENTATION
  monitor_stats.serialize_ns += monitor_elapsed_ns(step_start);
#endif
  len = size;

  if ((len + 2) > maxpacket)
  {
    log_error("Input buffer too long");
    return false;
  }

  // copy rpc data to call buffer
  // stlink is faster for word aligned access
  // call_buf is implemented as word aligned

#if TAGCORE_ENABLE_INSTRUMENTATION
  step_start = std::chrono::steady_clock::now();
#endif
  const bool write_ok = WriteMem32(call_buf, (uint8_t *)rpcbuf,
                                  (len + 3) & ~3);
#if TAGCORE_ENABLE_INSTRUMENTATION
  monitor_stats.write_request_ns += monitor_elapsed_ns(step_start);
#endif
  if (!write_ok)
  {
    log_error("RPC buffer write failed");
    return false;
  }

  // call monitor

  //std::cerr << "calling\n";

#if TAGCORE_ENABLE_INSTRUMENTATION
  step_start = std::chrono::steady_clock::now();
#endif
  const bool call_ok = Call(PROTOBUF, len, &retval);
#if TAGCORE_ENABLE_INSTRUMENTATION
  monitor_stats.monitor_call_ns += monitor_elapsed_ns(step_start);
#endif
  if (!call_ok)
  {
    log_error("monitor_call failed\n");
    return false;
  }

  len = retval;

  // retrieve protocol buffer -- round up to 4 byte boundary
  // stlink is faster for word aligned access

  bool read_ok = true;
#if TAGCORE_ENABLE_INSTRUMENTATION
  step_start = std::chrono::steady_clock::now();
#endif
  if (len) {
    read_ok = ReadMem32(call_buf, (uint8_t *)rpcbuf, (len + 3) & ~3);
  }
#if TAGCORE_ENABLE_INSTRUMENTATION
  monitor_stats.read_response_ns += monitor_elapsed_ns(step_start);
#endif
  if (!read_ok)
  {
    log_error("Error reading RPC buffer");
    return false;
  }
#if TAGCORE_ENABLE_INSTRUMENTATION
  step_start = std::chrono::steady_clock::now();
#endif
  const bool parse_ok = ack.ParseFromArray(rpcbuf, len);
#if TAGCORE_ENABLE_INSTRUMENTATION
  monitor_stats.parse_ns += monitor_elapsed_ns(step_start);
  monitor_stats.rpc_calls++;
  monitor_stats.request_bytes += size;
  monitor_stats.response_bytes += len;
  monitor_stats.rpc_total_ns += monitor_elapsed_ns(rpc_start);
#endif
  return parse_ok;
}

// Return git sha string -- read during attach()

bool TagMonitor::GitShaString(std::string &sha)
{
  sha.assign((char *)sha_str,sizeof(sha_str));
  return true;
}
