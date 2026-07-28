/**
 * @file handlersL4.c
 * @brief STM32L4 debug-monitor interrupt implementation.
 *
 * This file is included by handlers.c after the shared monitor state and
 * helpers have been declared.
 */

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
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_VC_CORERESET_Msk;
  monitorClearRequest();
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
 * main-thread context evaluates protobuf and writes the DCRDR result.
 */
void monitorServicePending(uint32_t monitor_events)
{
  int len = 0;
  uint32_t work = 0;
  bool do_eval = false;

  chSysLock();
  if (monitor_enabled && monitor_pending)
  {
    monitor_pending = false;
    if (monitorRequestPending())
    {
      len = monitor_operand;
      monitorDisarmTimeoutI();
      do_eval = true;
    }
  }
  else if ((monitor_events & EVT_MONITOR_TIMEOUT) && monitor_timeout_pending)
  {
    monitorStopI(true);
    chSysUnlock();
    return;
  }
  chSysUnlock();

  if (!do_eval)
    return;

  len = proto_eval(len, &work);

  if (work != 0U)
    chEvtAddEvents((eventmask_t)work);

  chSysLock();
  CoreDebug->DCRDR = monitor_enabled ? (uint32_t)len : 0U;
  monitorClearRequest();
  if (monitor_enabled)
    monitorArmTimeoutI(chTimeS2I(3), monitor_timeout_cb);
  chSysUnlock();
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
        monitorStartSessionI(chTimeS2I(3), monitor_timeout_cb);
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
        if (monitor_enabled && !monitor_pending) {
          monitor_operand = operand;
          monitor_pending = true;
          CoreDebug->DCRDR = 0;
          monitorArmTimeoutI(chTimeS2I(3), monitor_timeout_cb);
          if (tpMain)
            chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        } else {
          monitorClearRequest();
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
