/**
 * @file    handlersL4Mailbox.c
 * @brief   STM32L4 monitor over the shared-memory mailbox.
 *
 * @details Included by handlers.c, in place of handlersL4.c, on an STM32L4
 *          image built with @c TAG_L4_MONITOR_MAILBOX. Protobuf requests use
 *          the STM32U3 transport: the @c monitor_shared_t block at
 *          @c MONITOR_SHARED_ADDR, a kick on IRQ 39 (@c VectorDC, USART3 on the
 *          larger L4 parts and unused on the L432), and the
 *          @c host_activity heartbeat. The DebugMonitor is kept only for
 *          TAG_MONITORINFO, which the host's L4 attach uses before it knows
 *          the protocol; it answers MONITORPROTOCOL with
 *          @c MONITOR_PROTOCOL_MAILBOX and refuses MONITORSTART and PROTOBUF.
 *
 *          The session starts late, unlike on U3: the host attaches as on any
 *          L4 tag (vector catch, reset, the firmware boots), asks the
 *          protocol, then writes @c MONITOR_REQUEST_MAGIC to the block and
 *          pends the kick. The kick handler then publishes
 *          @c MONITOR_CONNECTED_MAGIC and starts the heartbeat. Between boot
 *          and that request the tag is kept awake by the attach grace, opened
 *          at boot when vector catch shows a host attaching under reset.
 *
 *          While connected the firmware keeps DMA1's clock on. On an STM32L4
 *          with the CPU the only enabled bus master, the bus matrix stops in
 *          Sleep, and every debug access to SRAM then reads zero as a success
 *          and drops writes, which would break the mailbox while the tag
 *          idles (embedded/tags/UIUCTag/TODO.md).
 *
 *          The service path, heartbeat and kick dispatch mirror handlersU3.c.
 *          They are kept separate for now so that the qualified STM32U375
 *          images do not move; see the core design document
 *          design/monitor-commands.md and the proposal
 *          design/proposals/l4-monitor-robustness.md.
 */

#include "persistent.h"

static_assert(sizeof(monitor_shared_t) <= MONITOR_SHARED_SIZE,
              "monitor shared block is too small");

/** @brief NVIC priority of the kick IRQ; must be ChibiOS kernel-callable. */
#ifndef TAG_DEBUG_MONITOR_PRIORITY
#define TAG_DEBUG_MONITOR_PRIORITY 8U
#endif

/** @brief The shared block, reserved below @c ram0 by the mailbox linker script. */
#define monitor_shared (*(volatile monitor_shared_t *)MONITOR_SHARED_ADDR)

/** @brief Seconds the tag stays awake after an attach for the host's session request. */
#define L4_MONITOR_ATTACH_TIMEOUT_S 10U

/** @brief True from boot or a TAG_MONITORINFO call until a session starts or the grace ends. */
static volatile bool monitor_attach_grace_active = false;

/** @brief True when this session turned DMA1's clock on, so the disconnect turns it off. */
static bool monitor_dma_clock_set = false;

static void monitor_timeout_cb(virtual_timer_t *vtp, void *arg);

static bool monitorAttachGraceActive(void)
{
  return monitor_attach_grace_active;
}

/**
 * @brief Ends the attach grace when its timer expires.
 *
 * @param[in] vtp Expired virtual timer, unused.
 * @param[in] arg Timer argument, unused.
 */
static void monitor_attach_timeout_cb(virtual_timer_t *vtp, void *arg)
{
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  monitor_attach_grace_active = false;
  chSysUnlockFromISR();
}

/**
 * @brief Opens, or extends, the attach grace.
 *
 * @details Does nothing once a session is running: the heartbeat owns the
 *          monitor timer then.
 *
 * @pre Called with the system locked.
 */
static void monitorStartAttachGraceI(void)
{
  if (monitor_enabled)
    return;

  monitor_attach_grace_active = true;
  monitorArmTimeoutI(chTimeS2I(L4_MONITOR_ATTACH_TIMEOUT_S),
                     monitor_attach_timeout_cb);
}

/**
 * @brief Opens the attach grace from the DebugMonitor handler.
 *
 * @details Before ChibiOS runs only the flag is set;
 *          monitorSharedSessionStart() arms its timer.
 */
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

/**
 * @brief Publishes a detached block and clears the host's debug state.
 *
 * @details Clears the session fields, vector catch and the DebugMonitor
 *          bits the host's L4 attach set, and turns DMA1's clock off if this
 *          session turned it on.
 *
 * @warning Call with the system locked, or before ChibiOS starts.
 */
static void monitorSharedSetDisconnected(void)
{
  CoreDebug->DEMCR &= ~(CoreDebug_DEMCR_VC_CORERESET_Msk |
                        CoreDebug_DEMCR_MON_REQ_Msk |
                        CoreDebug_DEMCR_MON_PEND_Msk |
                        CoreDebug_DEMCR_MON_EN_Msk);
  if (monitor_dma_clock_set)
  {
    RCC->AHB1ENR &= ~RCC_AHB1ENR_DMA1EN;
    monitor_dma_clock_set = false;
  }
  monitor_attach_grace_active = false;
  monitor_shared.request = 0U;
  monitor_shared.flags = 0U;
  monitor_shared.host_activity = 0U;
  monitor_shared.status = MONITOR_STATUS_IDLE;
  monitor_shared.command = 0U;
  monitor_shared.result = 0U;
  __DSB();
}

/**
 * @brief Starts a session for the host's request in the block.
 *
 * @details Turns DMA1's clock on (if it is off) so the bus matrix stays up
 *          for the debug port while the tag sleeps, starts the heartbeat,
 *          and publishes @c MONITOR_CONNECTED_MAGIC with the session-ready
 *          flag, which the host's attach waits for.
 *
 * @pre The system is locked; the block's metadata is published.
 */
static void monitorSharedConnectI(void)
{
  if ((RCC->AHB1ENR & RCC_AHB1ENR_DMA1EN) == 0U)
  {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    monitor_dma_clock_set = true;
  }
  monitor_attach_grace_active = false;
  monitorStartSessionI(chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
                       monitor_timeout_cb);
  monitor_shared.host_activity = 1U;
  monitor_shared.status = MONITOR_STATUS_IDLE;
  monitor_shared.command = 0U;
  monitor_shared.result = 0U;
  monitor_shared.flags |= MONITOR_SHARED_FLAG_SESSION_READY;
  __DMB();
  monitor_shared.request = MONITOR_CONNECTED_MAGIC;
}

/*
 * Public attachment state: the shared request word, as on U3. Vector catch is
 * not used here; the host leaves it set across sessions (see handlers.c).
 */
bool monitorIsAttached(void)
{
  return monitor_shared.request == MONITOR_CONNECTED_MAGIC;
}

/**
 * @brief Heartbeat check: ends the session when the host has gone quiet.
 *
 * @details As on U3: clears @c host_activity when set and checks again a
 *          period later; a period with no activity disconnects and signals
 *          the main thread.
 *
 * @param[in] vtp Expired virtual timer, unused.
 * @param[in] arg Timer argument, unused.
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
 * Cooperative protobuf service path, as on U3. The kick IRQ only latches the
 * request; main-thread context evaluates it and completes the block. Returns
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
    len = monitor_operand;
    monitorDisarmTimeoutI();
    do_eval = true;
  }
  else if ((monitor_events & EVT_MONITOR_TIMEOUT) && monitor_timeout_pending)
  {
    monitorStopSessionI();
    monitorSharedSetDisconnected();
    chSysUnlock();
    return false;
  }
  chSysUnlock();

  if (!do_eval)
    return false;

  len = proto_eval(len);

  chSysLock();
  monitor_shared.result = monitor_enabled ? (uint32_t)len : 0U;
  monitor_shared.status = monitor_enabled ? MONITOR_STATUS_DONE :
      MONITOR_STATUS_NOT_ATTACHED;
  monitor_shared.command = 0U;
  if (monitor_enabled)
    monitorArmTimeoutI(chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
                       monitor_timeout_cb);
  chSysUnlock();
  return true;
}

/*
 * Startup hooks. SRAM survives reset, so early init always publishes a
 * detached block: a request or CONNECTED word left by an earlier session must
 * not count. A host attaching under reset (vector catch set) opens the attach
 * grace, so the tag stays awake until the host asks for the session.
 */
void monitorSharedEarlyInit(void)
{
  monitor_shared.request = 0U;
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
  monitor_shared.path_magic = MONITOR_PATH_L4_MAGIC;
  __DSB();

  if (MONCONNECTED)
    monitor_attach_grace_active = true;
}

void monitorSharedSessionStart(void)
{
  /* Always enabled: the session request can come at any time after boot. */
  nvicEnableVector((IRQn_Type)MONITOR_SHARED_KICK_IRQN,
                   TAG_DEBUG_MONITOR_PRIORITY);

  if (!monitor_attach_grace_active)
    return;

  chSysLock();
  monitorStartAttachGraceI();
  chSysUnlock();
}

/*
 * The DebugMonitor, for TAG_MONITORINFO only. MONITORSTART and PROTOBUF are
 * refused with a zero result, so a host that does not know the mailbox fails
 * its attach rather than talking to a half-served monitor. MONITORSTOP still
 * ends a session, for a host that stops through the DebugMonitor.
 */
CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  uint32_t input = CoreDebug->DCRDR;
  uint8_t operation = (uint8_t)(input & 0xffU);
  int operand = (int)(input >> 8);

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk) != 0U) {
    switch (operation) {
      case TAG_MONITORINFO:
        monitorStartAttachGraceFromISR();
        switch (operand) {
          case MONITORVERSION:
            CoreDebug->DCRDR = (uint32_t)DEBUGVERSION_L4;
            break;
          case MONITORBUF:
            CoreDebug->DCRDR = (uint32_t)ProtoBuf;
            break;
          case MONITORBUFSIZE:
            CoreDebug->DCRDR = (uint32_t)sizeof(ProtoBuf);
            break;
          case TAGSHASTR:
            CoreDebug->DCRDR = (uint32_t)SHAStr;
            break;
          case MONITORPROTOCOL:
            CoreDebug->DCRDR = (uint32_t)MONITOR_PROTOCOL_MAILBOX;
            break;
          default:
            CoreDebug->DCRDR = 0U;
            break;
        }
        break;
      case MONITORSTOP:
        chSysLockFromISR();
        monitorStopSessionI();
        monitorSharedSetDisconnected();
        CoreDebug->DCRDR = 1U;
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        chSysUnlockFromISR();
        break;
      default:
        /* MONITORSTART, PROTOBUF and anything else: not over this path. */
        CoreDebug->DCRDR = 0U;
        break;
    }
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_REQ_Msk;
  }
  CH_IRQ_EPILOGUE();
}

/*
 * The kick IRQ (IRQ 39). As on U3, plus the late session request: a kick with
 * MONITOR_REQUEST_MAGIC in the request word starts the session.
 */
OSAL_IRQ_HANDLER(VectorDC)
{
  OSAL_IRQ_PROLOGUE();

  uint32_t command = monitor_shared.command;
  uint8_t operation = (uint8_t)(command & 0xffU);
  int operand = (int)(command >> 8);

  chSysLockFromISR();

  if (monitor_shared.request == MONITOR_REQUEST_MAGIC)
  {
    monitorSharedConnectI();
    operation = 0xffU; /* no command with the session request */
  }
  else if (monitor_shared.request == 0U)
  {
    /* Stale kick after detach: route it through MONITORSTOP's cleanup. */
    operation = MONITORSTOP;
  }
  else
  {
    monitor_shared.host_activity = 1U;
  }

  switch (operation) {
    case MONITORSTOP:
      /*
       * Explicit detach. The host takes request == 0 as completion, so the
       * block is cleared only after the session is stopped. The main thread is
       * signalled so it can sleep now that nothing is attached.
       */
      monitorStopSessionI();
      monitorSharedSetDisconnected();
      if (tpMain)
        chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
      break;
    case PROTOBUF:
      if (monitor_enabled && !monitor_pending) {
        /*
         * A plain get_status while the main thread is inside a guarded
         * region is answered here from cached state; every other request is
         * latched for the main thread.
         */
        if (!pState->safe) {
          int fast_len = monitor_fast_status_eval(operand);
          if (fast_len > 0) {
            monitor_shared.result = (uint32_t)fast_len;
            monitor_shared.status = MONITOR_STATUS_DONE;
            monitor_shared.command = 0U;
            break;
          }
        }
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
    case 0xffU:
      break;
    default:
      monitor_shared.status = MONITOR_STATUS_BAD_COMMAND;
      monitor_shared.command = 0U;
      break;
  }

  chSysUnlockFromISR();

  OSAL_IRQ_EPILOGUE();
}
