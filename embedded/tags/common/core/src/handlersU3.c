/**
 * @file handlersU3.c
 * @brief STM32U3 shared-memory monitor kick interrupt implementation.
 *
 * This file is included by handlers.c after the shared monitor state and
 * helpers have been declared.
 */

static_assert(sizeof(monitor_shared_t) <= MONITOR_SHARED_SIZE,
              "monitor shared block is too small");

#ifndef TAG_DEBUG_MONITOR_PRIORITY
#define TAG_DEBUG_MONITOR_PRIORITY 8U
#endif

#define monitor_shared (*(volatile monitor_shared_t *)MONITOR_SHARED_ADDR)

static bool monitor_requested_at_boot = false;

static void monitorSharedSetDisconnected(void)
{
  monitor_shared.request = 0U;
  monitor_shared.flags = 0U;
  monitor_shared.host_activity = 0U;
  monitor_shared.status = MONITOR_STATUS_IDLE;
  monitor_shared.command = 0U;
  monitor_shared.result = 0U;
}

/*
 * Public attachment state. U3 uses the shared request word as the truth.
 */
bool monitorIsAttached(void)
{
  return monitor_shared.request == MONITOR_CONNECTED_MAGIC;
}

/*
 * Session lifecycle and heartbeat. The host keeps the session alive by setting
 * host_activity; the timer clears it once and disconnects on the next quiet
 * tick.
 */
static void monitor_timeout_cb(virtual_timer_t *vtp, void *arg)
{
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  if (!monitor_enabled)
  {
    chSysUnlockFromISR();
    return;
  }

  monitor_shared.watchdog_ticks++;
  if (monitor_shared.request != MONITOR_CONNECTED_MAGIC)
  {
    monitorSharedSetDisconnected();
  }
  else if (monitor_shared.host_activity != 0U)
  {
    monitor_shared.host_activity = 0U;
    monitorArmTimeoutI(chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
                       monitor_timeout_cb);
    chSysUnlockFromISR();
    return;
  }
  monitorSharedSetDisconnected();

  if (monitor_enabled)
    monitor_timeout_pending = true;
  if (tpMain)
    chEvtSignalI(tpMain, EVT_MONITOR_TIMEOUT);
  chSysUnlockFromISR();
}

/*
 * Cooperative protobuf service path. The kick IRQ only latches work; main
 * thread context evaluates protobuf and completes the shared-memory command.
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
    len = monitor_operand;
    monitorDisarmTimeoutI();
    do_eval = true;
  }
  else if ((monitor_events & EVT_MONITOR_TIMEOUT) && monitor_timeout_pending)
  {
    /*
     * Heartbeat timeout: the host stopped polling, so tear down the local
     * session and publish detached state.
     */
    monitorStopSessionI();
    monitorSharedSetDisconnected();
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
  monitor_shared.result = monitor_enabled ? (uint32_t)len : 0U;
  monitor_shared.status = monitor_enabled ? MONITOR_STATUS_DONE :
      MONITOR_STATUS_NOT_ATTACHED;
  monitor_shared.command = 0U;
  if (monitor_enabled)
    monitorArmTimeoutI(chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
                       monitor_timeout_cb);
  chSysUnlock();
}

/*
 * Startup hooks. Early init publishes fixed metadata before ChibiOS starts;
 * session start arms the IRQ/timer once ChibiOS APIs are available.
 */
void monitorSharedEarlyInit(void)
{
  monitor_requested_at_boot =
      (monitor_shared.request == MONITOR_REQUEST_MAGIC);

  monitor_shared.abi_version = MONITOR_SHARED_ABI_VERSION;
  monitor_shared.debug_version = DEBUGVERSION;
  monitor_shared.flags = 0U;
  monitor_shared.buf_addr = (uint32_t)ProtoBuf;
  monitor_shared.buf_size = (uint32_t)sizeof(ProtoBuf);
  monitor_shared.sha_addr = (uint32_t)SHAStr;
  monitor_shared.command = 0U;
  monitor_shared.result = 0U;
  monitor_shared.status = MONITOR_STATUS_IDLE;
  monitor_shared.host_activity = 0U;
  monitor_shared.watchdog_ticks = 0U;
  monitor_shared.path_magic = MONITOR_PATH_U3_MAGIC;

  if (!monitor_requested_at_boot)
  {
    monitor_shared.request = 0U;
    return;
  }

  monitor_shared.host_activity = 1U;
  __DMB();
  monitor_shared.request = MONITOR_CONNECTED_MAGIC;
}

void monitorSharedSessionStart(void)
{
  if (!monitor_requested_at_boot)
    return;

  nvicEnableVector((IRQn_Type)MONITOR_SHARED_KICK_IRQN,
                   TAG_DEBUG_MONITOR_PRIORITY);

  chSysLock();
  monitorStartSessionI(chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
                       monitor_timeout_cb);
  monitor_shared.host_activity = 1U;
  monitor_shared.flags |= MONITOR_SHARED_FLAG_SESSION_READY;
  monitor_shared.status = MONITOR_STATUS_IDLE;
  chSysUnlock();
}

/*
 * U3 runtime monitor kick IRQ. Keep this at the end so the command dispatch is
 * read after the helpers it uses.
 */
OSAL_IRQ_HANDLER(STM32_FDCAN1_IT0_HANDLER)
{
  OSAL_IRQ_PROLOGUE();

  uint32_t command = monitor_shared.command;
  uint8_t operation = (uint8_t)(command & 0xffU);
  int operand = (int)(command >> 8);

  chSysLockFromISR();

  if (monitor_shared.request == 0U)
  {
    /*
     * Stale kick after detach. Route it through the MONITORSTOP case so
     * cleanup and detached-state publication stay in one place.
     */
    operation = MONITORSTOP;
  }
  else
  {
    monitor_shared.host_activity = 1U;
  }

  switch (operation) {
    case MONITORSTOP:
      /*
       * Explicit detach. The host polls request==0 as MONITORSTOP completion,
       * so publish that only after the timer/session has been stopped.
       */
      monitorStopSessionI();
      monitorSharedSetDisconnected();
      break;
    case PROTOBUF:
      if (monitor_enabled && !monitor_pending) {
        monitor_operand = operand;
        monitor_pending = true;
        monitor_shared.result = 0U;
        monitor_shared.status = MONITOR_STATUS_PENDING;
        monitor_shared.command = 0U;
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
      } else {
        monitor_shared.status = monitor_enabled ?
            MONITOR_STATUS_BUSY : MONITOR_STATUS_NOT_ATTACHED;
        monitor_shared.command = 0U;
      }
      break;
    default:
      monitor_shared.status = MONITOR_STATUS_BAD_COMMAND;
      monitor_shared.command = 0U;
      break;
  }

  chSysUnlockFromISR();

  OSAL_IRQ_EPILOGUE();
}
