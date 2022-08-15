#include "hal.h"
#include "app.h"
#include "monitor.h"
#include "version.h"
#include "ch.h"

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

uint8_t ProtoBuf[1024] __attribute__((aligned(4))) NOINIT;
const uint32_t protobuf_size = sizeof(ProtoBuf);
//CASSERT(sizeof(ProtoBuf) >= Ack_size)

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
    len = proto_eval(msg);
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
