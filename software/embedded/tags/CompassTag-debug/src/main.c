#include "hal.h"
#include "ch.h"
#include "monitor.h"
#include "rv3028.h"

#define DBGKEY (0xA05F << 16)
#define C_DEBUGEN (1 << 0)
#define C_HALT (1 << 1)
#define C_STEP (1 << 2)
#define C_MASKINTS (1 << 3)
#define S_REGRDY (1 << 16)
#define S_HALT (1 << 17)
#define S_SLEEP (1 << 18)
#define S_LOCKUP (1 << 19)
#define S_RETIRE_ST (1 << 24)
#define S_RESET_ST (1 << 25)

#define VC_CORERESET 1
#define MON_REQ (1 << 19)
#define MON_PEND (1 << 17)
#define MON_EN (1 << 16)

extern bool initRTC(void);
volatile uint32_t demcr;
binary_semaphore_t I2Cmutex;

static void delay(void){
  __NOP();
}

static inline void toInput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 0);
}

static inline void toOutput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 1 << (pin * 2));
}

static inline void toAlternate(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 2 << (pin * 2));
}

static inline void toAnalog(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 3 << (pin * 2));
}

#ifdef SWAP_I2C
const I2CConfig rtci2cConfig = {
    .delay = delay,
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA
};
#else
const I2CConfig rtci2cConfig = {
    .delay = delay,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL
};
#endif

void rtcOn(void)
{
  //chBSemWait(&I2Cmutex); 
  palSetLine(LINE_RTC_SDA);
  palSetLine(LINE_RTC_SCL);
  toOutput(LINE_RTC_SCL);
  toOutput(LINE_RTC_SDA);
  i2cStart(&I2CD1, &rtci2cConfig);
}

void rtcOff(void)
{
  i2cStop(&I2CD1);
  //chBSemSignal(&I2Cmutex); 
}

bool Call(uint8_t operation, int32_t operand, uint32_t *result){
  int i;
  CoreDebug->DCRDR = (operand << 8) | (operation & 0xff);
  demcr = CoreDebug->DEMCR;
  CoreDebug->DEMCR = demcr | MON_PEND | MON_REQ | VC_CORERESET;
  for (i = 0; i < 10 ; i++){
    demcr = CoreDebug->DEMCR;
    if (demcr & MON_REQ)
      break;
    chThdSleepMilliseconds(1);
  }
  *result = CoreDebug->DCRDR;
  return true;
}

void enable_monitor(void){
  demcr = CoreDebug->DEMCR;
  CoreDebug->DHCSR = DBGKEY;// | 1;
  CoreDebug->DEMCR = (demcr | MON_EN | VC_CORERESET) & ~MON_REQ & ~MON_PEND;
  demcr = CoreDebug->DEMCR;
}

int main(void)
{
  uint32_t version;
  char *hashstr;
  halInit();
  chSysInit();

  initRTC();

  enable_monitor();

  while (1)
  {
      
     Call(TAG_MONITORINFO, TAGSHASTR, (uint32_t *) &hashstr);
      chThdSleepMilliseconds(1);
      Call(TAG_MONITORINFO, MONITORVERSION,&version);
      chThdSleepMilliseconds(1);
  }
}
