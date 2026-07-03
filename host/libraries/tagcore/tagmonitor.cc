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
#define DCRSR 0xE000EDF4U
#define DCRDR 0xE000EDF8U
#define DEMCR 0xE000EDFCU

#define SCB_ICSR 0xE000ED04U
#define SCB_AIRCR 0xE000ED0CU
#define SCB_SCR 0xE000ED10U
#define SCB_SHPR1 0xE000ED18U
#define SCB_SHPR2 0xE000ED1CU
#define SCB_SHPR3 0xE000ED20U
#define SCB_SHCSR 0xE000ED24U
#define SCB_CFSR 0xE000ED28U
#define SCB_HFSR 0xE000ED2CU
#define SCB_DFSR 0xE000ED30U
#define SCB_MMFAR 0xE000ED34U
#define SCB_BFAR 0xE000ED38U
#define SCB_AFSR 0xE000ED3CU

#define STM32L4_DBGMCU_IDCODE 0xE0042000U
#define STM32L4_RCC_CSR 0x40021094U

#define STM32U3_DBGMCU_IDCODE 0xE0044000U
#define STM32U3_RCC_CSR 0x40030D14U

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

#ifndef TAGCORE_HALT_ON_MONITOR_TIMEOUT
#define TAGCORE_HALT_ON_MONITOR_TIMEOUT 1
#endif

#ifndef TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT
#define TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT 0
#endif


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

static const char *monitor_operation_name(uint8_t operation)
{
  switch (operation)
  {
  case TAG_MONITORINFO:
    return "TAG_MONITORINFO";
  case MONITORSTART:
    return "MONITORSTART";
  case MONITORSTOP:
    return "MONITORSTOP";
  case PROTOBUF:
    return "PROTOBUF";
  default:
    return "UNKNOWN";
  }
}

static const char *target_family_name(TargetFamily family)
{
  switch (family)
  {
  case TargetFamily::STM32L4:
    return "STM32L4";
  case TargetFamily::STM32U3:
    return "STM32U3";
  default:
    return "unknown";
  }
}

static bool plausible_stm32_idcode(uint32_t idcode)
{
  const uint32_t device_id = idcode & 0xfffU;
  return (device_id != 0U) && (device_id != 0xfffU);
}

bool TagMonitor::DetectTargetFamily()
{
  uint32_t idcode = 0;

  target_family = TargetFamily::Unknown;
  target_idcode = 0;

  if (ReadDebug32(STM32L4_DBGMCU_IDCODE, &idcode) && plausible_stm32_idcode(idcode))
  {
    target_family = TargetFamily::STM32L4;
    target_idcode = idcode;
    return true;
  }

  if (ReadDebug32(STM32U3_DBGMCU_IDCODE, &idcode) && plausible_stm32_idcode(idcode))
  {
    target_family = TargetFamily::STM32U3;
    target_idcode = idcode;
    return true;
  }

  return false;
}

bool TagMonitor::ReadTargetRccCsr(uint32_t *addr, uint32_t *value)
{
  switch (target_family)
  {
  case TargetFamily::STM32L4:
    *addr = STM32L4_RCC_CSR;
    return ReadMem32(*addr, (uint8_t *)value, sizeof(*value));
  case TargetFamily::STM32U3:
    *addr = STM32U3_RCC_CSR;
    return ReadMem32(*addr, (uint8_t *)value, sizeof(*value));
  default:
    *addr = 0;
    *value = 0xffffffffU;
    return false;
  }
}

bool TagMonitor::ReadCoreRegister(uint32_t reg, uint32_t *value)
{
  for (int i = 0; i < 100; i++)
  {
    uint32_t dhcsr = 0;
    if (ReadDebug32(DHCSR, &dhcsr) && (dhcsr & S_REGRDY))
      break;
    std::this_thread::sleep_for(MS(1));
  }

  if (!WriteDebug32(DCRSR, reg))
    return false;

  for (int i = 0; i < 100; i++)
  {
    uint32_t dhcsr = 0;
    if (ReadDebug32(DHCSR, &dhcsr) && (dhcsr & S_REGRDY))
      return ReadDebug32(DCRDR, value);
    std::this_thread::sleep_for(MS(1));
  }

  return false;
}

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
   * DCRDR is both the request mailbox and the response mailbox.  Do not write a
   * new request into it until the previous DebugMon transaction is fully idle.
   */
  if (demcr & (MON_PEND | MON_REQ)){
    log_error("monitor pending bit already set op=%s(0x%x) operand=%d demcr=0x%x",
              monitor_operation_name(operation), operation, operand, demcr);
    return false;
  }

  // write debug request data (operand,operation)

  if (!WriteDebug32(DCRDR, (operand << 8) | (operation & 0xff)))
  {
    log_error("write reg failed");
    return false;
  }

  if (!WriteDebug32(DEMCR, (demcr | MON_EN | MON_PEND | MON_REQ | VC_CORERESET)))
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
    uint32_t dhcsr_snapshot = 0;
    uint32_t dcrdr_snapshot = 0;
    uint32_t rcc_csr = 0xffffffffU;
    uint32_t rcc_csr_addr = 0;
    bool rcc_csr_ok = false;
    uint32_t icsr = 0;
    uint32_t aircr = 0;
    uint32_t scr = 0;
    uint32_t shpr1 = 0;
    uint32_t shpr2 = 0;
    uint32_t shpr3 = 0;
    uint32_t shcsr = 0;
    ReadDebug32(DHCSR, &dhcsr_snapshot);
    ReadDebug32(DCRDR, &dcrdr_snapshot);
    rcc_csr_ok = ReadTargetRccCsr(&rcc_csr_addr, &rcc_csr);
    ReadDebug32(SCB_ICSR, &icsr);
    ReadDebug32(SCB_AIRCR, &aircr);
    ReadDebug32(SCB_SCR, &scr);
    ReadDebug32(SCB_SHPR1, &shpr1);
    ReadDebug32(SCB_SHPR2, &shpr2);
    ReadDebug32(SCB_SHPR3, &shpr3);
    ReadDebug32(SCB_SHCSR, &shcsr);
    uint32_t vectactive = icsr & 0x1ffU;
    uint32_t rettobase = (icsr >> 11) & 1U;
    uint32_t vectpending = (icsr >> 12) & 0x1ffU;
    uint32_t isrpending = (icsr >> 22) & 1U;
    uint32_t halted_dhcsr = 0;
    uint32_t r13_sp = 0;
    uint32_t r14_lr = 0;
    uint32_t r15_pc = 0;
    uint32_t xpsr = 0;
    uint32_t msp = 0;
    uint32_t psp = 0;
    uint32_t special = 0;
    uint32_t cfsr = 0;
    uint32_t hfsr = 0;
    uint32_t dfsr = 0;
    uint32_t mmfar = 0;
    uint32_t bfar = 0;
    uint32_t afsr = 0;
    uint32_t msp_frame[8] = {0};
    uint32_t psp_frame[8] = {0};
    bool msp_frame_ok = false;
    bool psp_frame_ok = false;
    bool halted_by_timeout_probe = false;

    ReadDebug32(SCB_CFSR, &cfsr);
    ReadDebug32(SCB_HFSR, &hfsr);
    ReadDebug32(SCB_DFSR, &dfsr);
    ReadDebug32(SCB_MMFAR, &mmfar);
    ReadDebug32(SCB_BFAR, &bfar);
    ReadDebug32(SCB_AFSR, &afsr);

#if TAGCORE_HALT_ON_MONITOR_TIMEOUT
    if ((dhcsr_snapshot & S_LOCKUP) == 0U)
    {
      halted_by_timeout_probe = (dhcsr_snapshot & S_HALT) == 0U;
      if (WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT))
      {
        for (int halt_i = 0; halt_i < 100; halt_i++)
        {
          if (ReadDebug32(DHCSR, &halted_dhcsr) && (halted_dhcsr & S_HALT))
            break;
          std::this_thread::sleep_for(MS(1));
        }

        if (halted_dhcsr & S_HALT)
        {
          ReadCoreRegister(13, &r13_sp);
          ReadCoreRegister(14, &r14_lr);
          ReadCoreRegister(15, &r15_pc);
          ReadCoreRegister(16, &xpsr);
          ReadCoreRegister(17, &msp);
          ReadCoreRegister(18, &psp);
          ReadCoreRegister(20, &special);
          msp_frame_ok = ReadMem32(msp, (uint8_t *)msp_frame, sizeof(msp_frame));
          psp_frame_ok = ReadMem32(psp, (uint8_t *)psp_frame, sizeof(psp_frame));
        }
      }
    }
#endif

    if (dhcsr_snapshot & S_LOCKUP)
    {
      log_error("monitor call timed out op=%s(0x%x) operand=%d target=%s idcode=0x%x "
                "demcr=0x%x dhcsr=0x%x dcrdr=0x%x rcc_csr_addr=0x%x rcc_csr=0x%x rcc_csr_ok=%u "
                "icsr=0x%x active=%u pending=%u isrpending=%u rettobase=%u "
                "shcsr=0x%x shpr1=0x%x shpr2=0x%x shpr3=0x%x aircr=0x%x scr=0x%x "
                "halt_dhcsr=0x%x sp=0x%x lr=0x%x pc=0x%x xpsr=0x%x msp=0x%x psp=0x%x special=0x%x "
                "msp_frame_ok=%u msp_frame_lr=0x%x msp_frame_pc=0x%x msp_frame_xpsr=0x%x "
                "psp_frame_ok=%u psp_frame_lr=0x%x psp_frame_pc=0x%x psp_frame_xpsr=0x%x "
                "cfsr=0x%x hfsr=0x%x dfsr=0x%x mmfar=0x%x bfar=0x%x afsr=0x%x",
                monitor_operation_name(operation), operation, operand,
                target_family_name(target_family), target_idcode, demcr,
                dhcsr_snapshot, dcrdr_snapshot, rcc_csr_addr, rcc_csr,
                rcc_csr_ok ? 1U : 0U, icsr, vectactive, vectpending,
                isrpending, rettobase, shcsr, shpr1, shpr2, shpr3, aircr, scr,
                halted_dhcsr, r13_sp, r14_lr, r15_pc, xpsr, msp, psp, special,
                msp_frame_ok ? 1U : 0U, msp_frame[5], msp_frame[6], msp_frame[7],
                psp_frame_ok ? 1U : 0U, psp_frame[5], psp_frame[6], psp_frame[7],
                cfsr, hfsr, dfsr, mmfar, bfar, afsr);
    }
    else
    {
      log_error("monitor call timed out op=%s(0x%x) operand=%d target=%s idcode=0x%x "
                "demcr=0x%x dhcsr=0x%x dcrdr=0x%x rcc_csr_addr=0x%x rcc_csr=0x%x rcc_csr_ok=%u "
                "icsr=0x%x active=%u pending=%u isrpending=%u rettobase=%u "
                "shcsr=0x%x shpr1=0x%x shpr2=0x%x shpr3=0x%x aircr=0x%x scr=0x%x "
                "halt_dhcsr=0x%x sp=0x%x lr=0x%x pc=0x%x xpsr=0x%x msp=0x%x psp=0x%x special=0x%x "
                "msp_frame_ok=%u msp_frame_lr=0x%x msp_frame_pc=0x%x msp_frame_xpsr=0x%x "
                "psp_frame_ok=%u psp_frame_lr=0x%x psp_frame_pc=0x%x psp_frame_xpsr=0x%x "
                "cfsr=0x%x hfsr=0x%x dfsr=0x%x mmfar=0x%x bfar=0x%x afsr=0x%x",
                monitor_operation_name(operation), operation, operand,
                target_family_name(target_family), target_idcode, demcr,
                dhcsr_snapshot, dcrdr_snapshot, rcc_csr_addr, rcc_csr,
                rcc_csr_ok ? 1U : 0U, icsr, vectactive, vectpending,
                isrpending, rettobase, shcsr, shpr1, shpr2, shpr3, aircr, scr,
                halted_dhcsr, r13_sp, r14_lr, r15_pc, xpsr, msp, psp, special,
                msp_frame_ok ? 1U : 0U, msp_frame[5], msp_frame[6], msp_frame[7],
                psp_frame_ok ? 1U : 0U, psp_frame[5], psp_frame[6], psp_frame[7],
                cfsr, hfsr, dfsr, mmfar, bfar, afsr);
    }

    bool reset_vector_catch =
        ((dhcsr_snapshot & (S_HALT | S_RESET_ST)) == (S_HALT | S_RESET_ST)) &&
        (vectactive == 0U) &&
        (cfsr == 0U) &&
        (hfsr == 0U);

    if (reset_vector_catch)
    {
      /*
       * The target reset while the monitor session was active. The firmware
       * monitor thread and protobuf buffer state are gone, so continuing to
       * issue PROTOBUF calls only re-pends DebugMon at Reset_Handler. Clear
       * vector catch before resuming, then drop the host-side session; the next
       * useful operation must attach again and run MONITORSTART.
       */
      uint32_t resume_demcr =
          (demcr | MON_EN) & ~MON_REQ & ~MON_PEND & ~VC_CORERESET;
      if (WriteDebug32(DEMCR, resume_demcr) &&
          WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN))
      {
        uint32_t resume_dhcsr = 0;
        for (int resume_i = 0; resume_i < 20; resume_i++)
        {
          if (ReadDebug32(DHCSR, &resume_dhcsr) && ((resume_dhcsr & S_HALT) == 0U))
            break;
          std::this_thread::sleep_for(MS(1));
        }
        if ((resume_dhcsr & S_HALT) == 0U)
        {
          log_error("monitor caught target reset; cleared vector catch and released reset handler pc=0x%x target=%s rcc_csr_addr=0x%x rcc_csr=0x%x dhcsr=0x%x",
                    r15_pc, target_family_name(target_family), rcc_csr_addr,
                    rcc_csr, resume_dhcsr);
        }
        else
        {
          log_error("monitor caught target reset; target still halted after release pc=0x%x target=%s rcc_csr_addr=0x%x rcc_csr=0x%x dhcsr=0x%x",
                    r15_pc, target_family_name(target_family), rcc_csr_addr,
                    rcc_csr, resume_dhcsr);
        }
      }
      else
      {
        log_error("monitor caught target reset but failed to resume target");
      }
      maxpacket = 0;
      call_buf = 0;
      memset(sha_str, 0, sizeof(sha_str));
      version = 0;
      target_family = TargetFamily::Unknown;
      target_idcode = 0;
      LinkAdapt::Detach();
    }
#if TAGCORE_HALT_ON_MONITOR_TIMEOUT && !TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT
    else if (halted_by_timeout_probe && ((halted_dhcsr & S_HALT) != 0U))
    {
      uint32_t resume_demcr = (demcr | MON_EN | VC_CORERESET) & ~MON_REQ & ~MON_PEND;
      if (WriteDebug32(DEMCR, resume_demcr) &&
          WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN))
      {
        log_error("monitor timeout diagnostics halted target; cleared pending request and resumed core");
      }
      else
      {
        log_error("monitor timeout diagnostics halted target but failed to resume it");
      }
    }
#endif
    return false;
  }

  if (demcr & MON_REQ){
    log_error("monitor pending bit still set op=%s(0x%x) operand=%d demcr=0x%x",
              monitor_operation_name(operation), operation, operand, demcr);
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
      log_error("monitor result read timed out op=%s(0x%x) operand=%d demcr=0x%x",
                monitor_operation_name(operation), operation, operand, demcr);
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
  maxpacket = 0;
  memset(sha_str, 0, sizeof(sha_str));
  version = 0;
  target_family = TargetFamily::Unknown;
  target_idcode = 0;

  do
  {
    // connect to stlink

    if (!LinkAdapt::Attach(usbdev))
    {
      log_error("Attach failed");
      return false;
    }

    std::this_thread::sleep_for(MS(20));

    DetectTargetFamily();
    log_debug("Target family: %s idcode=0x%x",
              target_family_name(target_family), target_idcode);

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
  {
    maxpacket = 0;
    call_buf = 0;
    memset(sha_str,0, sizeof(sha_str));
    version = 0;
    target_family = TargetFamily::Unknown;
    target_idcode = 0;
    return;
  }

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
  target_family = TargetFamily::Unknown;
  target_idcode = 0;
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
