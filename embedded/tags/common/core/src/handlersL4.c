/**
 * @file handlersL4.c
 * @brief STM32L4 debug-monitor interrupt implementation.
 *
 * This file is included by handlers.c after the shared monitor state and
 * helpers have been declared.
 */

#include "persistent.h"

/*
 * Legacy DebugMonitor request bits.
 */
static bool monitorRequestPending(void)
{
  return (CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk) != 0U;
}

static void monitorClearRequest(void)
{
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_REQ_Msk;
}

/*
 * Session lifecycle. L4 uses DEMCR/DCRDR directly, with a short watchdog while
 * a protobuf request is outstanding.
 */
#define L4_MONITOR_TIMEOUT_S 60U
#define L4_MONITOR_ATTACH_TIMEOUT_S 10U

static volatile bool monitor_attach_grace_active = false;

/**
 * @brief True while the main thread is evaluating a latched request.
 *
 * @details Covers the window in monitorServicePending() after it has cleared
 *          @c monitor_pending and before it writes the reply and clears
 *          @c MON_REQ. Together with @c monitor_pending it says a request is
 *          in flight from latch to reply, so the DebugMonitor handler neither
 *          drops it nor encodes a reply into @c ProtoBuf under it.
 */
static volatile bool monitor_serving = false;

static bool monitorAttachGraceActive(void)
{
  return monitor_attach_grace_active;
}

static void monitor_attach_timeout_cb(virtual_timer_t *vtp, void *arg)
{
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  monitor_attach_grace_active = false;
  chSysUnlockFromISR();
}

static void monitorStartAttachGraceI(void)
{
  if (monitor_enabled)
    return;

  monitor_attach_grace_active = true;
  monitorArmTimeoutI(chTimeS2I(L4_MONITOR_ATTACH_TIMEOUT_S),
                     monitor_attach_timeout_cb);
}

static void monitorStartAttachGraceFromISR(void)
{
  if (!tpMain)
  {
    monitor_attach_grace_active = true;
    return;
  }

  chSysLockFromISR();
  monitorStartAttachGraceI();
  chSysUnlockFromISR();
}

static void monitor_timeout_cb(virtual_timer_t *vtp, void *arg)
{
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  if (monitor_enabled)
    monitor_timeout_pending = true;
  if (tpMain)
    chEvtSignalI(tpMain, EVT_MONITOR_TIMEOUT);
  chSysUnlockFromISR();
}

static void monitorStopI(bool timed_out)
{
  (void)timed_out;

  monitorStopSessionI();
  monitor_attach_grace_active = false;
  monitorClearRequest();
  CoreDebug->DEMCR &= ~(CoreDebug_DEMCR_VC_CORERESET_Msk |
                        CoreDebug_DEMCR_MON_PEND_Msk |
                        CoreDebug_DEMCR_MON_EN_Msk);
  __DSB();
}

/*
 * Public attachment state.
 */
bool monitorIsAttached(void)
{
  return monitor_enabled &&
         ((CoreDebug->DEMCR & CoreDebug_DEMCR_VC_CORERESET_Msk) != 0U);
}

/*
 * Cooperative protobuf service path. The interrupt latches the request length;
 * main-thread context evaluates protobuf and writes the DCRDR result. Returns
 * true when a request was served.
 */
bool monitorServicePending(uint32_t monitor_events)
{
  int len = 0;
  bool do_eval = false;

  chSysLock();
  if (monitor_enabled && monitor_pending)
  {
    monitor_pending = false;
    if (monitorRequestPending())
    {
      len = monitor_operand;
      monitorDisarmTimeoutI();
      monitor_serving = true;
      do_eval = true;
    }
  }
  else if ((monitor_events & EVT_MONITOR_TIMEOUT) && monitor_timeout_pending)
  {
    monitorStopI(true);
    chSysUnlock();
    return false;
  }
  chSysUnlock();

  if (!do_eval)
    return false;

  len = proto_eval(len);

  chSysLock();
  CoreDebug->DCRDR = monitor_enabled ? (uint32_t)len : 0U;
  monitorClearRequest();
  monitor_serving = false;
  if (monitor_enabled)
    monitorArmTimeoutI(chTimeS2I(L4_MONITOR_TIMEOUT_S), monitor_timeout_cb);
  chSysUnlock();
  return true;
}

/*
 * L4 has no shared-memory early attach phase. These hooks exist so common
 * startup can call the same monitor entry points for both processor families.
 */
void monitorSharedEarlyInit(void)
{
}

void monitorSharedSessionStart(void)
{
  if (!monitor_attach_grace_active)
    return;

  chSysLock();
  monitorStartAttachGraceI();
  chSysUnlock();
}

/*
 * L4 DebugMonitor command handler. Keep the actual interrupt entry point at the
 * end so the request helpers, lifecycle, and service path are visible first.
 */
CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  uint32_t input = (CoreDebug->DCRDR);
  uint8_t operation = (input & 0xff);
  int operand = (input >> 8);

  // could use operand for packet length

  if (monitorRequestPending()) {
    switch (operation) {
      case TAG_MONITORINFO:
        monitorStartAttachGraceFromISR();
        switch (operand) {
          case (MONITORVERSION):
            CoreDebug->DCRDR = (uint32_t)DEBUGVERSION;
            break;
          case (MONITORBUF):
            CoreDebug->DCRDR = (uint32_t)ProtoBuf;
            break;
          case (MONITORBUFSIZE):
            CoreDebug->DCRDR = (uint32_t)sizeof(ProtoBuf);
            break;
          case (TAGSHASTR):
            CoreDebug->DCRDR = (uint32_t) SHAStr;
            break;
        }
        monitorClearRequest();
        break;
      case MONITORSTART:
        chSysLockFromISR();
        monitorStartSessionI(chTimeS2I(L4_MONITOR_TIMEOUT_S), monitor_timeout_cb);
        CoreDebug->DCRDR = 1;
        monitorClearRequest();
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        chSysUnlockFromISR();
        break;
      case MONITORSTOP:
        chSysLockFromISR();
        CoreDebug->DCRDR = 1U;
        monitorStopI(false);
        chSysUnlockFromISR();
        break;
      case PROTOBUF:  // execute later in main-thread context
        chSysLockFromISR();
        if (!monitor_enabled) {
          /* Outside a session: complete it empty, so the host does not hang. */
          monitorClearRequest();
        } else if (monitor_pending || monitor_serving) {
          /*
           * A request is already in flight. Most often this is the same
           * request triggered twice -- the host repeating a MON_PEND write
           * after an SWD fault that had in fact landed. Leave MON_REQ set:
           * it belongs to the request in flight, whose reply the main thread
           * writes and then clears it. Clearing it here made the host read
           * DCRDR as an empty reply, and the main thread then found MON_REQ
           * clear and dropped the real request.
           */
        } else {
          /*
           * When the main thread is inside a guarded region it cannot serve
           * the request until it yields. A plain get_status is read-only, so
           * it is answered here from cached state, as on U3; any other
           * request is latched for the main thread.
           */
          int fast_len = pState->safe ? 0 : monitor_fast_status_eval(operand);
          if (fast_len > 0) {
            CoreDebug->DCRDR = (uint32_t)fast_len;
            monitorClearRequest();
            chSysUnlockFromISR();
            break;
          }
          monitor_operand = operand;
          monitor_pending = true;
          CoreDebug->DCRDR = 0;
          monitorArmTimeoutI(chTimeS2I(L4_MONITOR_TIMEOUT_S), monitor_timeout_cb);
          if (tpMain)
            chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        }
        chSysUnlockFromISR();
        break;
      default:
        monitorClearRequest();
        break;
    }
  }
  CH_IRQ_EPILOGUE();
}
