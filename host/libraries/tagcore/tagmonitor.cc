#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <cstddef>
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
#define SCB_VTOR 0xE000ED08U
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

static uint32_t monitor_command_word(uint8_t operation, int32_t operand)
{
  return ((uint32_t)operand << 8) | (operation & 0xffU);
}

static bool monitor_shared_any_nonzero(const monitor_shared_t &shared)
{
  return (shared.request != 0U) ||
         (shared.abi_version != 0U) ||
         (shared.debug_version != 0U) ||
         (shared.flags != 0U) ||
         (shared.buf_addr != 0U) ||
         (shared.buf_size != 0U) ||
         (shared.sha_addr != 0U) ||
         (shared.command != 0U) ||
         (shared.result != 0U) ||
         (shared.status != 0U) ||
         (shared.host_activity != 0U) ||
         (shared.watchdog_ticks != 0U) ||
         (shared.path_magic != 0U);
}

bool TagMonitor::ReadMemWord(uint32_t addr, uint32_t *value)
{
  return ReadMem32(addr, (uint8_t *)value, sizeof(*value));
}

bool TagMonitor::WriteMemWord(uint32_t addr, uint32_t value)
{
  return WriteMem32(addr, (uint8_t *)&value, sizeof(value));
}

bool TagMonitor::ReadMonitorShared(monitor_shared_t &shared)
{
  return ReadMem32(MONITOR_SHARED_ADDR, (uint8_t *)&shared, sizeof(shared));
}

bool TagMonitor::WriteMonitorSharedWord(size_t offset, uint32_t value)
{
  return WriteMemWord(MONITOR_SHARED_ADDR + (uint32_t)offset, value);
}

void TagMonitor::LogMonitorShared(const char *prefix,
                                  const monitor_shared_t &shared)
{
  uint32_t raw[MONITOR_SHARED_SIZE / sizeof(uint32_t)] = {};
  bool raw_ok = ReadMem32(MONITOR_SHARED_ADDR, (uint8_t *)raw, sizeof(raw));

  log_error("%s fields: request=0x%x abi=%u debug=0x%x "
            "flags=0x%x buf=0x%x size=%u sha=0x%x command=0x%x "
            "result=0x%x status=0x%x heartbeat=0x%x watchdog_ticks=%u "
            "path=0x%x",
            prefix, shared.request, shared.abi_version,
            shared.debug_version, shared.flags, shared.buf_addr,
            shared.buf_size, shared.sha_addr, shared.command, shared.result,
            shared.status, shared.host_activity, shared.watchdog_ticks,
            shared.path_magic);

  if (raw_ok)
  {
    log_error("%s raw: %08x %08x %08x %08x %08x %08x %08x %08x "
              "%08x %08x %08x %08x %08x %08x %08x %08x",
              prefix, raw[0], raw[1], raw[2], raw[3],
              raw[4], raw[5], raw[6], raw[7],
              raw[8], raw[9], raw[10], raw[11],
              raw[12], raw[13], raw[14], raw[15]);
  }
  else
  {
    log_error("%s raw: shared block read failed", prefix);
  }
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
  if (target_family == TargetFamily::STM32U3)
    return CallU3(operation, operand, result);

  static const int MONITOR_RESULT_TIMEOUT_MS = 500;
  static const int MONITORSTOP_POST_MS = 5;
  static const int MONITOR_FAST_CALL_TIMEOUT_MS = 2500;
  static const int MONITOR_PROTOBUF_CALL_TIMEOUT_MS = 65000;
  const int call_timeout_ms = (operation == PROTOBUF)
                                  ? MONITOR_PROTOBUF_CALL_TIMEOUT_MS
                                  : MONITOR_FAST_CALL_TIMEOUT_MS;
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

  if (operation == MONITORSTOP)
  {
    /*
     * Detach intentionally lets the target drop into terminal low power as
     * soon as the DebugMonitor handler has seen the request. Waiting for a
     * DCRDR response or post-stop DEMCR read can therefore turn a successful
     * detach into a host-side USB/SWD timeout.
     */
    std::this_thread::sleep_for(MS(MONITORSTOP_POST_MS));
    if (result)
      *result = 1U;
    return true;
  }

  // wait for result by polling MON_REQ bit

  for (i = 0; i < call_timeout_ms; i++)
  {
    if (!ReadDebug32(DEMCR, &demcr))
      log_error("read_mem failed\n");
    else if (!(demcr & MON_REQ))
      break;
    std::this_thread::sleep_for(MS(1));
  }

  // check for timeout

  if (i == call_timeout_ms)
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
    for (i = 0; i < MONITOR_RESULT_TIMEOUT_MS; i++)
    {
      if (ReadDebug32(DCRDR, result))
        break;
      log_error("read_mem failed");
      std::this_thread::sleep_for(MS(1));
    }

    // check for a timeout

    if (i == MONITOR_RESULT_TIMEOUT_MS - 1)
    {
      log_error("monitor result read timed out op=%s(0x%x) operand=%d demcr=0x%x",
                monitor_operation_name(operation), operation, operand, demcr);
      return false;
    }
  }
  //log_debug("operation = 0x%x operand = 0x%x result = 0x%x\n",operation, operand, *result);
  return true;
}

bool TagMonitor::AttachL4()
{
  do
  {
    auto reset_into_monitor = [this](const char *phase) {
      uint32_t setup_demcr;

      if (!ReadDebug32(DEMCR, &setup_demcr))
      {
        log_error("Monitor attach failed: %s DEMCR read failed", phase);
        return false;
      }

      /*
       * Standby can power-cycle the core debug block. Program vector catch
       * immediately before reset release so the firmware sees MONCONNECTED
       * before it can return to terminal standby.
       */
      if (!(WriteDebug32(DBG_HCSR, DBGKEY | C_DEBUGEN) &&
            WriteDebug32(DEMCR,
                ((setup_demcr | MON_EN | VC_CORERESET) &
                  ~MON_REQ & ~MON_PEND))))
      {
        log_error("Monitor attach failed: %s debug setup failed", phase);
        return false;
      }

      if (!AssertReset(true))
      {
        log_error("Monitor attach failed: %s reset release failed", phase);
        return false;
      }

      std::this_thread::sleep_for(MS(50));

      if (!WriteDebug32(DBG_HCSR, DBGKEY | C_DEBUGEN))
        log_warn("Monitor attach warning: %s halt release failed", phase);

      return true;
    };

    if (!reset_into_monitor("initial"))
    {
      LinkAdapt::Detach();
      break;
    }

    // Call monitor to get pointer to information block

    if (!Call(TAG_MONITORINFO, MONITORVERSION, &version))
    {
      log_warn("L4 monitor version read failed; retrying attach after reset pulse");

      if (!AssertReset(false))
      {
        log_error("Monitor attach failed: retry reset assert failed");
        LinkAdapt::Detach();
        break;
      }

      std::this_thread::sleep_for(MS(20));

      if (!reset_into_monitor("retry") ||
          !Call(TAG_MONITORINFO, MONITORVERSION, &version))
      {
        log_error("couldn't fetch monitor version information");
        LinkAdapt::Detach();
        break;
      }
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

  return false;
}

bool TagMonitor::AttachU3()
{
  static const int ATTACH_TIMEOUT_MS = 2000;
  uint32_t demcr = 0;
  uint32_t empty[MONITOR_SHARED_SIZE / sizeof(uint32_t)] = {};
  monitor_shared_t shared = {};
  monitor_shared_t last_nonzero_shared = {};
  bool saw_nonzero_shared = false;

  do
  {
    if (!ReadDebug32(DEMCR, &demcr))
    {
      log_error("U3 monitor attach failed: DEMCR read failed");
      break;
    }

    if (!(WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN) &&
          WriteDebug32(DEMCR,
              ((demcr | VC_CORERESET) &
                ~MON_EN & ~MON_REQ & ~MON_PEND))))
    {
      log_error("U3 monitor attach failed: debug setup failed");
      break;
    }

    if (!AssertReset(true))
    {
      log_error("U3 monitor attach failed: reset release failed");
      break;
    }

    uint32_t dhcsr = 0;
    for (int i = 0; i < 100; i++)
    {
      if (ReadDebug32(DHCSR, &dhcsr) && (dhcsr & S_HALT))
        break;
      std::this_thread::sleep_for(MS(1));
    }

    if ((dhcsr & S_HALT) == 0U)
    {
      log_error("U3 monitor attach failed: target did not halt at reset vector dhcsr=0x%x",
                dhcsr);
      break;
    }

    if (!WriteMem32(MONITOR_SHARED_ADDR, (uint8_t *)&empty, sizeof(empty)) ||
        !WriteMonitorSharedWord(offsetof(monitor_shared_t, request),
                                MONITOR_REQUEST_MAGIC) ||
        !WriteMemWord(MONITOR_SHARED_KICK_ICPR_ADDR,
                      MONITOR_SHARED_KICK_MASK))
    {
      log_error("U3 monitor attach failed: shared request setup failed");
      break;
    }

    if (ReadMonitorShared(shared))
    {
      log_debug("U3 monitor request written at reset vector request=0x%x "
                "flags=0x%x status=0x%x dhcsr=0x%x",
                shared.request, shared.flags, shared.status, dhcsr);
    }
    else
    {
      log_error("U3 monitor attach warning: reset-vector shared readback failed");
    }

    if (!WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN))
    {
      log_error("U3 monitor attach failed: resume failed");
      break;
    }

    bool connected = false;
    for (int i = 0; i < ATTACH_TIMEOUT_MS; i++)
    {
      WriteMonitorSharedWord(offsetof(monitor_shared_t, host_activity), 1U);
      if (ReadMonitorShared(shared))
      {
        if (monitor_shared_any_nonzero(shared))
        {
          last_nonzero_shared = shared;
          saw_nonzero_shared = true;
        }
        if ((shared.request == MONITOR_CONNECTED_MAGIC) &&
            ((shared.flags & MONITOR_SHARED_FLAG_SESSION_READY) != 0U))
        {
          connected = true;
          break;
        }
      }
      std::this_thread::sleep_for(MS(1));
    }

    if (!connected)
    {
      uint32_t ispr = 0;
      uint32_t dhcsr_snapshot = 0;
      uint32_t demcr_snapshot = 0;
      ReadMonitorShared(shared);
      ReadMemWord(MONITOR_SHARED_KICK_ISPR_ADDR, &ispr);
      ReadDebug32(DHCSR, &dhcsr_snapshot);
      ReadDebug32(DEMCR, &demcr_snapshot);
      log_error("U3 monitor attach timed out target=%s idcode=0x%x "
                "request=0x%x flags=0x%x status=0x%x "
                "heartbeat=0x%x watchdog_ticks=%u "
                "dhcsr=0x%x demcr=0x%x ispr=0x%x",
                target_family_name(target_family), target_idcode,
                shared.request, shared.flags, shared.status,
                shared.host_activity, shared.watchdog_ticks,
                dhcsr_snapshot, demcr_snapshot, ispr);
      LogMonitorShared("U3 monitor attach timeout shared", shared);
      if (saw_nonzero_shared)
        LogMonitorShared("U3 monitor attach last nonzero shared",
                         last_nonzero_shared);
      break;
    }

    if (shared.abi_version != MONITOR_SHARED_ABI_VERSION)
    {
      log_error("U3 monitor ABI mismatch host=%u target=%u",
                MONITOR_SHARED_ABI_VERSION, shared.abi_version);
      break;
    }

    version = shared.debug_version;
    call_buf = shared.buf_addr;
    maxpacket = shared.buf_size;

    if ((version != DEBUGVERSION) || (call_buf == 0U) || (maxpacket == 0U) ||
        (maxpacket > sizeof(rpcbuf)) || (shared.sha_addr == 0U))
    {
      log_error("U3 monitor metadata invalid version=0x%x buf=0x%x size=%zu sha=0x%x",
                version, call_buf, maxpacket, shared.sha_addr);
      break;
    }

    if (!ReadMem32(shared.sha_addr, (uint8_t *)sha_str, sizeof(sha_str)))
    {
      log_error("U3 monitor attach failed: SHA read failed");
      break;
    }

    log_debug("U3 monitor attached version=0x%x buf=0x%x size=%zu sha=%s",
              version, call_buf, maxpacket, sha_str);
    return true;
  } while (0);

  call_buf = 0;
  maxpacket = 0;
  memset(sha_str, 0, sizeof(sha_str));
  version = 0;
  return false;
}

bool TagMonitor::Attach(UsbDev usbdev)
{
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
    if (!LinkAdapt::Attach(usbdev))
    {
      log_error("Attach failed");
      return false;
    }

    std::this_thread::sleep_for(MS(20));

    DetectTargetFamily();
    log_debug("Target family: %s idcode=0x%x",
              target_family_name(target_family), target_idcode);

    if (target_family == TargetFamily::STM32U3)
    {
      if (AttachU3())
        return true;
      LinkAdapt::Detach();
      break;
    }

    if (AttachL4())
      return true;

    LinkAdapt::Detach();
  } while (0);

  Detach();
  return false;
}

bool TagMonitor::CallU3(uint8_t operation, int32_t operand, uint32_t *result)
{
  static const int TIMEOUT_MS = 2500;
  static const int MONITORSTOP_TIMEOUT_MS = 100;
  monitor_shared_t shared = {};

  if (!IsAttached())
  {
    log_error("monitor not attached");
    return false;
  }

  if (!ReadMonitorShared(shared))
  {
    log_error("U3 monitor shared read failed");
    return false;
  }

  if ((operation != MONITORSTOP) &&
      (shared.request != MONITOR_CONNECTED_MAGIC))
  {
    log_error("U3 monitor not connected request=0x%x status=0x%x",
              shared.request, shared.status);
    return false;
  }

  if ((operation != MONITORSTOP) &&
      (shared.command != 0U || shared.status == MONITOR_STATUS_PENDING))
  {
    log_error("U3 monitor busy command=0x%x status=0x%x",
              shared.command, shared.status);
    return false;
  }

  const uint32_t command = monitor_command_word(operation, operand);
  if (!WriteMonitorSharedWord(offsetof(monitor_shared_t, result), 0U) ||
      !WriteMonitorSharedWord(offsetof(monitor_shared_t, status),
                              MONITOR_STATUS_IDLE) ||
      !WriteMonitorSharedWord(offsetof(monitor_shared_t, command), command) ||
      !WriteMemWord(MONITOR_SHARED_KICK_ICPR_ADDR,
                    MONITOR_SHARED_KICK_MASK) ||
      !WriteMemWord(MONITOR_SHARED_KICK_ISPR_ADDR,
                    MONITOR_SHARED_KICK_MASK))
  {
    log_error("U3 monitor kick failed op=%s(0x%x) operand=%d",
              monitor_operation_name(operation), operation, operand);
    return false;
  }

  const int timeout_ms = (operation == MONITORSTOP) ? MONITORSTOP_TIMEOUT_MS :
                                                     TIMEOUT_MS;
  for (int i = 0; i < timeout_ms; i++)
  {
    uint32_t status = 0;
    if (ReadMemWord(MONITOR_SHARED_ADDR + offsetof(monitor_shared_t, status),
                    &status))
    {
      if (status == MONITOR_STATUS_DONE)
      {
        if (result)
        {
          if (!ReadMemWord(MONITOR_SHARED_ADDR +
                               offsetof(monitor_shared_t, result),
                           result))
          {
            log_error("U3 monitor result read failed");
            return false;
          }
        }
        return true;
      }

      if (operation == MONITORSTOP)
      {
        monitor_shared_t stop_shared = {};
        if (ReadMonitorShared(stop_shared) &&
            (stop_shared.request == 0U) &&
            (stop_shared.flags == 0U) &&
            (stop_shared.command == 0U))
        {
          log_debug("U3 monitor stop observed detached state status=0x%x "
                    "result=0x%x heartbeat=0x%x watchdog_ticks=%u",
                    stop_shared.status, stop_shared.result,
                    stop_shared.host_activity, stop_shared.watchdog_ticks);
          if (result)
            *result = 1U;
          return true;
        }
      }

      if ((status == MONITOR_STATUS_BUSY) ||
          (status == MONITOR_STATUS_BAD_COMMAND) ||
          (status == MONITOR_STATUS_NOT_ATTACHED))
      {
        log_error("U3 monitor request rejected op=%s(0x%x) operand=%d status=0x%x",
                  monitor_operation_name(operation), operation, operand,
                  status);
        return false;
      }
    }
    std::this_thread::sleep_for(MS(1));
  }

  if (operation == MONITORSTOP)
  {
    /*
     * U3 detach is allowed to race the terminal sleep path. Firmware handles
     * MONITORSTOP by clearing the shared monitor block, clearing vector-catch
     * debug state, and signalling the main thread to enter terminal sleep. If
     * standby wins before the host observes the cleared shared block, further
     * SWD reads can fail or stall even though detach did what it needed to do.
     */
    log_warn("U3 monitor stop did not observe detached state before timeout; "
             "completing host detach");
    if (result)
      *result = 1U;
    return true;
  }

  uint32_t ispr = 0;
  uint32_t iser = 0;
  uint32_t iabr = 0;
  uint32_t dhcsr_snapshot = 0;
  uint32_t demcr_snapshot = 0;
  uint32_t icsr = 0;
  uint32_t vtor = 0;
  uint32_t shcsr = 0;
  uint32_t cfsr = 0;
  uint32_t hfsr = 0;
  uint32_t dfsr = 0;
  uint32_t r13_sp = 0;
  uint32_t r14_lr = 0;
  uint32_t r15_pc = 0;
  uint32_t xpsr = 0;
  uint32_t msp = 0;
  uint32_t psp = 0;
  uint32_t special = 0;
  uint32_t halted_dhcsr = 0;
  bool halted_by_timeout_probe = false;
  ReadMonitorShared(shared);
  ReadMemWord(MONITOR_SHARED_KICK_ISPR_ADDR - 0x100U, &iser);
  ReadMemWord(MONITOR_SHARED_KICK_ISPR_ADDR, &ispr);
  ReadMemWord(0xE000E304U, &iabr);
  ReadDebug32(DHCSR, &dhcsr_snapshot);
  ReadDebug32(DEMCR, &demcr_snapshot);
  ReadDebug32(SCB_ICSR, &icsr);
  ReadDebug32(SCB_VTOR, &vtor);
  ReadDebug32(SCB_SHCSR, &shcsr);
  ReadDebug32(SCB_CFSR, &cfsr);
  ReadDebug32(SCB_HFSR, &hfsr);
  ReadDebug32(SCB_DFSR, &dfsr);

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
      }
    }
  }
#endif

  log_error("U3 monitor call timed out op=%s(0x%x) operand=%d "
            "request=0x%x command=0x%x status=0x%x result=0x%x "
            "flags=0x%x iser=0x%x ispr=0x%x iabr=0x%x "
            "dhcsr=0x%x demcr=0x%x icsr=0x%x vtor=0x%x shcsr=0x%x "
            "cfsr=0x%x hfsr=0x%x dfsr=0x%x halt_dhcsr=0x%x "
            "sp=0x%x lr=0x%x pc=0x%x xpsr=0x%x msp=0x%x psp=0x%x special=0x%x",
            monitor_operation_name(operation), operation, operand,
            shared.request, shared.command, shared.status, shared.result,
            shared.flags, iser, ispr, iabr, dhcsr_snapshot, demcr_snapshot,
            icsr, vtor, shcsr, cfsr, hfsr, dfsr, halted_dhcsr,
            r13_sp, r14_lr, r15_pc, xpsr, msp, psp, special);

#if TAGCORE_HALT_ON_MONITOR_TIMEOUT && !TAGCORE_LEAVE_HALTED_ON_MONITOR_TIMEOUT
  if (halted_by_timeout_probe && ((halted_dhcsr & S_HALT) != 0U))
  {
    if (!WriteDebug32(DHCSR, DBGKEY | C_DEBUGEN))
      log_error("U3 monitor timeout diagnostics halted target but failed to resume it");
  }
#endif
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

  if (target_family == TargetFamily::STM32U3)
  {
    uint32_t stop_success = 0;
    const bool stop_ok = Call(MONITORSTOP, 0, &stop_success);
    monitor_shared_t detached_shared = {};
    if (ReadMonitorShared(detached_shared))
    {
      log_debug("U3 monitor after MONITORSTOP request=0x%x flags=0x%x "
                "status=0x%x command=0x%x heartbeat=0x%x watchdog_ticks=%u "
                "stop_ok=%u stop_success=%u",
                detached_shared.request, detached_shared.flags,
                detached_shared.status, detached_shared.command,
                detached_shared.host_activity, detached_shared.watchdog_ticks,
                stop_ok ? 1U : 0U, stop_success);
    }
    if (!stop_ok || !stop_success)
    {
      log_error("U3 Monitor Stop failed during detach");
    }
  }
  else
  {
    uint32_t stop_success = 0;
    if (!Call(MONITORSTOP, 0, &stop_success) || !stop_success)
    {
      log_error("Monitor Stop failed during detach");
    }
  }

  // Firmware clears monitor debug bits while handling MONITORSTOP; after that
  // it may immediately return to low power, so avoid racing the target with
  // best-effort debug-register cleanup from the host.
  if ((target_family == TargetFamily::STM32U3) ||
      (target_family == TargetFamily::STM32L4))
  {
    log_debug("%s monitor detach leaves post-stop debug cleanup to target firmware",
              target_family_name(target_family));
  }
  else if (ReadDebug32(DEMCR, &demcr))
  {
    const uint32_t detached_demcr =
        demcr & ~(VC_CORERESET | MON_PEND | MON_REQ | MON_EN);
    if (!WriteDebug32(DEMCR, detached_demcr))
    {
      log_error("Monitor detach failed to clear debug control register");
    }
    else
    {
      uint32_t verify_demcr = 0;
      if (ReadDebug32(DEMCR, &verify_demcr) &&
          ((verify_demcr & (VC_CORERESET | MON_PEND | MON_REQ | MON_EN)) != 0U))
      {
        log_error("Monitor detach left debug control bits set demcr=0x%x",
                  verify_demcr);
      }
    }
  }
  else
  {
    log_error("Monitor detach failed to read debug control register");
  }

  // release usb
  LinkAdapt::Detach();
  maxpacket = 0;
  call_buf = 0;
  memset(sha_str,0, sizeof(sha_str));
  version = 0;
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
