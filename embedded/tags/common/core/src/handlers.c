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
  chVTSetI(&monitor_timer, chTimeS2I(60), monitor_timeout_cb, NULL);
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

#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER
  if (monitor_clock_fast)
  {
    slow_msi();
    monitor_clock_fast = false;
  }
#endif

  if (timed_out)
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_VC_CORERESET_Msk;
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_REQ_Msk;
}

uint32_t monitorServicePending(uint32_t monitor_events)
{
  int len = 0;
  uint32_t work = 0;
  bool do_eval = false;

  chSysLock();
  if (monitor_enabled && monitor_pending)
  {
    monitor_pending = false;
    if (CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk)
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
    return 0;
  }
  chSysUnlock();

  if (!do_eval)
    return 0;

  len = proto_eval(len, &work);

  chSysLock();
  CoreDebug->DCRDR = monitor_enabled ? (uint32_t)len : 0U;
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_REQ_Msk;
  if (monitor_enabled)
    monitorArmTimeoutI();
  chSysUnlock();

  return work;
}

bool monitorNeedsService(void)
{
  return monitor_pending;
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
CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  uint32_t input = (CoreDebug->DCRDR);
  uint8_t operation = (input & 0xff);
  int operand = (input >> 8);

  // could use operand for packet length

  if (CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk) {
    CoreDebug->DCRDR = 0;

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
        (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        break;
      case MONITORSTART:
        chSysLockFromISR();
        monitor_enabled = true;
        monitor_pending = false;
        monitor_timeout_pending = false;
        monitorArmTimeoutI();
#if defined(RANGE_MULTIPLIER) && RANGE_MULTIPLIER
        if (!monitor_clock_fast)
        {
          fast_msi();
          monitor_clock_fast = true;
        }
#endif
        CoreDebug->DCRDR = 1;
        (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        chSysUnlockFromISR();
        break;
      case MONITORSTOP:
        chSysLockFromISR();
        monitorStopI(false);
        chSysUnlockFromISR();
        break;
      case PROTOBUF:  // execute later in main-thread context
        chSysLockFromISR();
        if (monitor_enabled && !monitor_pending) {
          monitor_operand = operand;
          monitor_pending = true;
          CoreDebug->DCRDR = 0;
          monitorArmTimeoutI();
          if (tpMain)
            chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        } else {
          (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        }
        chSysUnlockFromISR();
        break;
      default:
        (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        break;
    }
  }
  CH_IRQ_EPILOGUE();
}
/** @} */
