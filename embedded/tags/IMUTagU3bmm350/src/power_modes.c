#include "ch.h"
#include "hal.h"
#include "core_types.h"
#include "monitor.h"
#include "spi_bus.h"

static bool doWFI = false;
static volatile bool wfi_sleep_startup_complete = false;

static inline bool idle_wfi_allowed(void)
{
  return wfi_sleep_startup_complete && !monitorIsAttached();
}

bool is_main_waiting_on_timeout(void) {
    tstate_t state;

    if (tpMain == NULL) {
        return false;
    }

    chSysLock();
    // In ChibiOS, access the state directly from the thread pointer
    state = tpMain->state;
    chSysUnlock();

    // Any thread blocked via a sleep API or an OS object timeout 
    // transitions directly into CH_STATE_SLEEPING.
    return (state == CH_STATE_SLEEPING);
}

void idle_enter(void){
  /* --- 2. Enable Clocks in Sleep Mode --- */
  RCC->AHB1SLPENR1 |= RCC_AHB1SLPENR1_GPDMA1SLPEN | RCC_AHB1SLPENR1_SRAM1SLPEN;
  RCC->APB2SLPENR  |= RCC_APB2SLPENR_SPI1SLPEN;

  /* Dummy read to ensure RCC write finishes before moving on */
  (void)RCC->APB2ENR;

  doWFI = idle_wfi_allowed();

  //palEnableLineEvent(LINE_WKUP1, PAL_EVENT_MODE_RISING_EDGE);
  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
  // Force DBGMCU clock to remain active during Core Sleep

  if (monitorIsAttached()){
    //DBGMCU->CR |= 1;;
    //palSetLine(LINE_testpin);
  } else {
    //palClearLine(LINE_testpin);
  }
   //palSetLine(LINE_testpin);
};

void idle_loop(void){
  doWFI = idle_wfi_allowed();
  if (doWFI) {
    palSetLine(LINE_testpin);
    __DSB();
    __WFI();
    __ISB();
  }
}

void idle_leave(void){
  if (doWFI) {
    //DBGMCU->CR &= ~1; // sleep bit is not defined in stm32u375xx.h
  }
  palClearLine(LINE_testpin);
}

void idle_enable_wfi_sleep(void)
{
  wfi_sleep_startup_complete = true;
}
