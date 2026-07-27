/**
 * @file handlers.c
 * @brief Debug monitor interrupt and cooperative protobuf request handling.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "monitor.h"
#include "version.h"
#include "ch.h"
#include "assert.h"
#include "tag.pb.h"

#include "core_types.h"
#include "core_events.h"
#include "core_sync.h"
#include "custom.h"

#define xstr(s) str(s)
#define str(s) #s

#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx)
#define TAG_HANDLERS_PROCESSOR_U3 1
#define TAG_HANDLERS_PROCESSOR_L4 0
#elif defined(STM32L4xx_MCUCONF) || defined(STM32L422xx) || \
      defined(STM32L431xx) || defined(STM32L432xx) || \
      defined(STM32L433xx)
#define TAG_HANDLERS_PROCESSOR_U3 0
#define TAG_HANDLERS_PROCESSOR_L4 1
#else
#error "handlers.c supports only STM32L4 and STM32U3 tag processors"
#endif

/** @name Debug monitor globals
 *  Debug Monitor Interface
 * Shared buffers and monitor metadata used by the DebugMon handler and helper
 * main thread to exchange protobuf packets with the host monitor.
 * @{
 */

static const char SHAStr[64] __attribute__((aligned(4))) = GIT_SHA ;

/**
 * @brief Evaluate one protobuf monitor request from the shared buffer.
 *
 * @param[in] len Number of request bytes in ProtoBuf.
 * @return Encoded acknowledgement byte count.
 */
extern int proto_eval(int, uint32_t *);

#ifndef PROTOBUFSIZE
#define PROTOBUFSIZE 2056
#endif

static_assert(Ack_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Ack_size));
static_assert(Req_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Req_size));


uint8_t ProtoBuf[PROTOBUFSIZE] __attribute__((aligned(4))) NOINIT;
const int protobuf_size = PROTOBUFSIZE;
/** @} */


#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER

/** @name Optional debug clock scaling
 * Clock helpers speed monitor transfers during debugging and restore the normal
 * low-power clock when the monitor session ends.
 * @{
 */
/**
 * @brief Raise MSI and system-tick rates for faster monitor traffic.
 */
static void fast_msi(void){
  // change to 24Mhz doesn't require VOS change
  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | FLASH_WS_FAST;

  // Change MSI frequency P 197 RM0394

  RCC->CR = (RCC->CR & ~(15<<4)) |  STM32_MSIRANGE_FAST;

  // Change TIM2 Prescaler

  STM32_ST_TIM->PSC =  ((STM32_TIMCLK2 * RANGE_MULTIPLIER)/ OSAL_ST_FREQUENCY) - 1;;

}

/**
 * @brief Restore MSI and system-tick rates after monitor traffic ends.
 */
static void slow_msi(void){

 
   // Restore MSI frequency P 197 RM0394

   RCC->CR = (RCC->CR & ~(15<<4)) | STM32_MSIRANGE;

  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | FLASH_WS_SLOW;

  // Restore TIM2 Prescaler

  STM32_ST_TIM->PSC =  (STM32_TIMCLK2 / OSAL_ST_FREQUENCY) - 1;

}
/** @} */

#endif


/** @name Cooperative monitor request state
 * DebugMon only latches protobuf work and signals the main thread. The main
 * state-machine path evaluates monitor requests at cooperative safe points.
 * @{
 */
static volatile bool monitor_enabled = false;
static volatile bool monitor_pending = false;
static volatile bool monitor_timeout_pending = false;
static int monitor_operand = 0;
static virtual_timer_t monitor_timer;
static bool monitor_timer_initialized = false;
#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER
static bool monitor_clock_fast = false;
#endif

#if TAG_HANDLERS_PROCESSOR_U3
static inline bool monitorRequestPending(void)
{
  return (CoreDebug->DEMCR &
          (CoreDebug_DEMCR_MON_REQ_Msk | CoreDebug_DEMCR_MON_PEND_Msk)) != 0U;
}

static inline void monitorClearRequest(void)
{
  CoreDebug->DEMCR &= ~(CoreDebug_DEMCR_MON_REQ_Msk |
                        CoreDebug_DEMCR_MON_PEND_Msk);
}
#else
static inline bool monitorRequestPending(void)
{
  return (CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk) != 0U;
}

static inline void monitorClearRequest(void)
{
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_REQ_Msk;
}
#endif

static inline bool monitorServiceRequestReady(void)
{
#if TAG_HANDLERS_PROCESSOR_U3
  return true;
#else
  return monitorRequestPending();
#endif
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

static void monitorArmTimeoutI(void)
{
  if (!monitor_timer_initialized)
  {
    chVTObjectInit(&monitor_timer);
    monitor_timer_initialized = true;
  }
  monitor_timeout_pending = false;
  chVTSetI(&monitor_timer, chTimeS2I(3), monitor_timeout_cb, NULL);
}

static void monitorDisarmTimeoutI(void)
{
  if (monitor_timer_initialized)
    chVTResetI(&monitor_timer);
}

static void monitorStopI(bool timed_out)
{
  monitorDisarmTimeoutI();
  monitor_timeout_pending = false;
  monitor_enabled = false;
  monitor_pending = false;
  //palClearLine(LINE_testpin);

#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER
  if (monitor_clock_fast)
  {
    slow_msi();
    monitor_clock_fast = false;
  }
#endif

  (void)timed_out;
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_VC_CORERESET_Msk;
  monitorClearRequest();
  __DSB();
}

bool monitorIsAttached(void)
{
  return monitor_enabled &&
         ((CoreDebug->DEMCR & CoreDebug_DEMCR_VC_CORERESET_Msk) != 0U);
}

bool isMonitorEnabled(void)
{
  return monitorIsAttached();
}

void monitorServicePending(uint32_t monitor_events)
{
  int len = 0;
  uint32_t work = 0;
  bool do_eval = false;

  chSysLock();
  if (monitor_enabled && monitor_pending)
  {
    monitor_pending = false;
    if (monitorServiceRequestReady())
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
    monitorArmTimeoutI();
  chSysUnlock();
}

void monitorPostPendingEvents(void)
{
  eventmask_t pending_events = 0U;

  chSysLock();
  if (monitor_pending)
    pending_events |= EVT_MONITOR_SERVICE;
  if (monitor_timeout_pending)
    pending_events |= EVT_MONITOR_TIMEOUT;
  if (pending_events != 0U)
    chEvtAddEventsI(pending_events);
  chSysUnlock();
}

/** @} */

/** @name Debug monitor interrupt
 * Interrupt entry point used by the host monitor to discover buffers, start or
 * stop monitor sessions, and hand protobuf packet lengths to the main thread.
 * @{
 */
/**
 * @brief Service debug-monitor commands from the host tooling.
 */
#if TAG_HANDLERS_PROCESSOR_U3
#include "handlersU3.c"
#else
#include "handlersL4.c"
#endif
/** @} */
