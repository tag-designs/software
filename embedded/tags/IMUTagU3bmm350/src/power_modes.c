#include "ch.h"
#include "hal.h"
#include "core_types.h"
#include "monitor.h"
#include "spi_bus.h"


void idle_enter(void){

  // This should use the correct mode depending upon a system state variable

  /* --- 2. Enable Clocks in Sleep Mode --- */
  /* these appear on by default
  RCC->AHB1SLPENR1 |= RCC_AHB1SLPENR1_GPDMA1SLPEN | RCC_AHB1SLPENR1_SRAM1SLPEN;
  RCC->APB2SLPENR  |= RCC_APB2SLPENR_SPI1SLPEN;
  */ 
  /* Dummy read to ensure RCC write finishes before moving on */
  //(void)RCC->APB2ENR;

  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
};

void idle_loop(void){
  if (!monitorIsAttached()){
    palSetLine(LINE_testpin);
    __DSB();
    __WFI();
    __ISB();
  }
}

void idle_leave(void){
  palClearLine(LINE_testpin);
  // should undo sleepmode configuration
}

