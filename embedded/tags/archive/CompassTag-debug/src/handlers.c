#include "hal.h"
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

int call_count = 0;


CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  uint32_t input = (CoreDebug->DCRDR);
  uint8_t operation = (input & 0xff);
  int operand = (input >> 8);
  uint32_t demcr = CoreDebug->DEMCR;
  CoreDebug->DCRDR = 3333;
  call_count++;
  // could use operand for packet length

  //if (demcr & ((CoreDebug_DEMCR_MON_EN_Msk) | (CoreDebug_DEMCR_MON_REQ_Msk))) {
   if (demcr & ((CoreDebug_DEMCR_MON_REQ_Msk))) { 
   CoreDebug->DCRDR = 4444;

    switch (operation) {
      case TAG_MONITORINFO:
        switch (operand) {
          case (MONITORVERSION):
            CoreDebug->DCRDR = (uint32_t)DEBUGVERSION;
            break;
          case (MONITORBUF):
            CoreDebug->DCRDR = (uint32_t)1111;
            break;
          case (MONITORBUFSIZE):
            CoreDebug->DCRDR = (uint32_t)2222;
            break;
          case (TAGSHASTR):
            CoreDebug->DCRDR = (uint32_t) SHAStr;
            break;
        }
      case MONITORSTART:
      case MONITORSTOP:
      case PROTOBUF:  // execute with helper thread
      default:
        //CoreDebug->DEMCR = demcr & ~(CoreDebug_DEMCR_MON_EN_Msk | CoreDebug_DEMCR_MON_REQ_Msk);
        CoreDebug->DEMCR = demcr & ~(CoreDebug_DEMCR_MON_REQ_Msk);
        break;
    }
  }
  CH_IRQ_EPILOGUE();
}
