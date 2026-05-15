#include "hal.h"
#include "app.h"
#include "monitor.h"
#include "version.h"
#include "ch.h"
#include "assert.h"

#define xstr(s) str(s)
#define str(s) #s


/***********************************
 *  Debug Monitor Interface
 ***********************************/

// GIT SHA string

static const char SHAStr[64] __attribute__((aligned(4))) = GIT_SHA ;
// evaluation function and buffer

extern int proto_eval(int);

// buffer for passing protobuf messages

#ifndef PROTOBUFSIZE
#define PROTOBUFSIZE 2056
#endif

static_assert(Ack_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Ack_size));
static_assert(Req_size < PROTOBUFSIZE, "Protocol buffer is too small! " xstr(PROTOBUFSIZE) " " xstr(Req_size));


uint8_t ProtoBuf[PROTOBUFSIZE] __attribute__((aligned(4))) NOINIT;
const int protobuf_size = PROTOBUFSIZE;

// P137 in RM0394

static void fast_msi(void){
  // change to 24Mhz doesn't require VOS change
  // Program VOS bits to "10" in PWR_CR1 register

  //PWR->CR1 = (PWR->CR1 & ~(3<<9)) | (2<<9);

  // Wait until VOSF flag is cleared in PWR_SR2 register

 // while ((PWR->SR2 & PWR_SR2_VOSF) != 0)    /* Wait until regulator is      */
  //

  // Adjust Wait States

  FLASH->ACR = (FLASH->ACR & ~(7)) | 3;

  // Change MSI frequency P 197 RM0394

  RCC->CR = (RCC->CR & ~(15<<4)) | (9<<4);

  // Change TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC * 12;

}


static void slow_msi(void){

  // Change MSI frequency

   // Change MSI frequency P 197 RM0394

   RCC->CR = (RCC->CR & ~(15<<4)) | (5<<4);

  // Adjust Wait States

  FLASH->ACR = FLASH->ACR & ~(7);

  // Program VOS bits to "10" in PWR_CR1 register

  //PWR->CR1 = (PWR->CR1 & ~(3<<9)) | (1<<9);

  // Restore TIM2 Prescaler

  STM32_ST_TIM->PSC = STM32_ST_TIM->PSC/12;

}


// Internal thread

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

  // system locked !!

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
    palSetLine(LINE_ProtoHandler);
    len = proto_eval(msg);
    palClearLine(LINE_ProtoHandler);
    chSysLock();
    CoreDebug->DCRDR = len;
    (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
    // system locked !!
  }

  // system locked
  //   clear monitor thread pointers
  tpMonitor = NULL;
  trp = NULL;
  // acknowledge and exit
  (CoreDebug->DEMCR) &= ~CoreDebug_DEMCR_MON_REQ_Msk;
  chThdExitS(0);
}

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
