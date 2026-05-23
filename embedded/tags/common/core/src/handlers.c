/**
 * @file handlers.c
 * @brief Debug monitor interrupt and helper thread for protobuf request handling.
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
#include "custom.h"
#include "debug_log.h"

#define xstr(s) str(s)
#define str(s) #s


/** @name Debug monitor globals
 *  Debug Monitor Interface
 * Shared buffers and monitor metadata used by the DebugMon handler and helper
 * thread to exchange protobuf packets with the host monitor.
 * @{
 */

static const char SHAStr[64] __attribute__((aligned(4))) = GIT_SHA ;

/**
 * @brief Evaluate one protobuf monitor request from the shared buffer.
 *
 * @param[in] len Number of request bytes in ProtoBuf.
 * @return Encoded acknowledgement byte count.
 */
extern int proto_eval(int);

#ifndef PROTOBUFSIZE
#define PROTOBUFSIZE 2056
#endif

static_assert(Ack_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Ack_size));
static_assert(Req_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Req_size));


uint8_t ProtoBuf[PROTOBUFSIZE] __attribute__((aligned(4))) NOINIT;
const int protobuf_size = PROTOBUFSIZE;
/** @} */


#ifdef RANGE_MULTIPLIER

/** @name Optional debug clock scaling
 * Clock helpers speed monitor transfers during debugging and restore the normal
 * low-power clock before the helper thread exits.
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


/** @name Monitor helper thread
 * The DebugMon ISR stays short by waking this thread to run protobuf decoding
 * and command evaluation outside interrupt context.
 * @{
 */
static thread_t *tpMonitor = NULL;
static thread_reference_t trp NOINIT;  // for synchronous wait

static THD_FUNCTION(MonitorThread, arg);
static THD_WORKING_AREA(waMonitor, 1024) NOINIT;
static const thread_descriptor_t monitor_descriptor = {
    "monitor",
    THD_WORKING_AREA_BASE(waMonitor),
    THD_WORKING_AREA_END(waMonitor),
    NORMALPRIO, 
    MonitorThread,
    NULL};

/**
 * @brief Evaluate monitor protobuf requests and acknowledge completion to DebugMon.
 *
 * @param[in] arg Unused ChibiOS thread argument.
 */
static THD_FUNCTION(MonitorThread, arg) {
  (void)arg;
  msg_t msg;
  int len;

  // chRegSetThreadName("Monitor");

  // acknowledge MONITORSTART

  chSysLock();
  tpMonitor = chThdGetSelfX();
  CoreDebug->DCRDR = 1;
  (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;

  debug_log_init();

  // system locked !!

  #ifdef RANGE_MULTIPLIER
  fast_msi();
  #endif

  while (1) {
    // system locked !!

    msg = chThdSuspendTimeoutS(&trp,chTimeS2I(60));

    if (msg == MSG_RESET) {
      break;
    }
    if (msg == MSG_TIMEOUT) {
      // timeout -- force end of debug
      CoreDebug->DEMCR &=  ~CoreDebug_DEMCR_VC_CORERESET_Msk;
      break;
    }
    chSysUnlock();
    len = proto_eval(msg);
    chSysLock();
    CoreDebug->DCRDR = len;
    (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
    // system locked !!
  }

  #ifdef RANGE_MULTIPLIER
  slow_msi();
  #endif

  // system locked
  //   clear monitor thread pointers
  tpMonitor = NULL;
  trp = NULL;
  // acknowledge and exit
  (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
  chThdExitS(0);
}
/** @} */

/** @name Debug monitor interrupt
 * Interrupt entry point used by the host monitor to discover buffers, start or
 * stop the helper thread, and hand protobuf packet lengths to it.
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
        if (!tpMonitor) {
          chSysLockFromISR();
          trp = NULL;
          tpMonitor = NULL;
          chThdCreateI(&monitor_descriptor);
          chSysUnlockFromISR();
          // thread handles acknowledgement
        } else {
          (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        }
        break;
      case MONITORSTOP:
        if (tpMonitor) {
          chSysLockFromISR();
          chThdResumeI(&trp, MSG_RESET);  // exit
          chSysUnlockFromISR();
        }
        // thread clears req when it exits
        break;
      case PROTOBUF:  // execute with helper thread
        if (tpMonitor) {
          chSysLockFromISR();
          CoreDebug->DCRDR = 0;
          chThdResumeI(&trp, (msg_t)operand);  // eval
          chSysUnlockFromISR();
        } else {
          (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        }
        break;
      default:
        (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
        break;
    }
  }
  CH_IRQ_EPILOGUE();
}
/** @} */
