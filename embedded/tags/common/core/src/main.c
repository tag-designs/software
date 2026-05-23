/**
 * @file main.c
 * @brief Core tag firmware boot, initialization, event polling, and main loop.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "tag.pb.h"

#include "adc.h"
#include "core_events.h"
#include "core_runtime.h"
#include "core_sync.h"
#include "core_types.h"
#include "custom.h"
#include "device.h"
#include "persistent.h"
#include "power.h"
#include "rtc_api.h"
#include "timekeeping.h"

#include "config.h"

/** @name Shared runtime synchronization
 * Semaphores and timestamps shared by core services and device drivers.
 * @{
 */
binary_semaphore_t ADCmutex;
binary_semaphore_t SPImutex;
binary_semaphore_t I2Cmutex;
binary_semaphore_t USARTmutex;

thread_t *tpMain = 0;
int32_t timestamp;
uint32_t timestamp_millis;

volatile BackupState *const pState = (BackupState *)&RTC->BKP0R;
/** @} */

#define ADC_SMPR_SMP_47P5 4

/** @name Board health measurement
 * Helpers used by monitor status and persistent state markers to record supply
 * voltage and die temperature without exposing ADC channel sequencing.
 * @{
 */
/**
 * @brief Measure the MCU supply voltage and internal temperature.
 *
 * @param[out] vdd100 Supply voltage in hundredths of a volt.
 * @param[out] temp10 Die temperature in tenths of a degree Celsius.
 */
void adcVDD(uint16_t *vdd100, int16_t *temp10)
{
  uint32_t raw;
  int32_t tmp;
  uint32_t adc_samples[2];

  uint16_t TS_CAL1 = *((uint16_t *)0x1FFF75A8);
  uint16_t TS_CAL2 = *((uint16_t *)0x1FFF75CA);
  uint16_t VREF_CAL = *((uint16_t *)0x1FFF75AA);

  chBSemWait(&ADCmutex);
  adc1Start();
  adc1EnableVREF();
  adc1EnableTS();
  //chThdSleepMilliseconds(1);
  adc1StartConversion(0, ADC_SMPR_SMP_47P5);
  while (adc1Eoc() == false)
    ;
  adc_samples[0] = adc1DR();
  adc1StartConversion(17, ADC_SMPR_SMP_47P5);
  while (adc1Eoc() == false)
    ;
  adc_samples[1] = adc1DR();
  adc1Stop();
  chBSemSignal(&ADCmutex);

  // compute vdd*100

  raw = adc_samples[0];
  tmp = (300 * VREF_CAL) / raw;
  *vdd100 = (uint16_t) tmp;

  // compute temperature *10

  raw = adc_samples[1];

  // voltage compensated temperature sensor reading

  tmp = (((1300-300)*raw * tmp)/300);

  // temperature * 10

  *temp10 = (tmp - (1300-300) * TS_CAL1)/ (TS_CAL2 - TS_CAL1) + 300;
}
/** @} */

/** @name Runtime initialization and reset handling
 * Boot helpers decide whether retained state is usable, reset device state when
 * needed, and map STM32 reset flags to the state-machine reset vocabulary.
 * @{
 */
/**
 * @brief Initialize board devices and retained runtime state when needed.
 *
 * @param[in] force Nonzero to force reinitialization after reset handling.
 */
void deviceInit(int force)
{
  // it's not clear from the reference manual that this is the correct
  // way to detect power on reset.  It doesn't seem that the
  // tag is reseting properly when power is disconnected !
  // -- might check RTC status bit for "unconfigured"

  if ((pState->resetCause == resetPower) ||
      (pState->resetCause == resetBrownout) || force)
  {
    // make sure everything is zeroed

    pState->valid = false;
    pState->safe = false;
  
    // configure RTC

    tagRtcInit();

    tagDevicesDeinit();

    // turn off all alarms

    disableAllAlarms();
    disableTicker();
    pState->valid = true;
  }
}

/**
 * @brief Classify the reset reason from STM32 flags and retained backup state.
 *
 * Because we use standby mode and because attaching the monitor may
 * trigger a reset, it is important to determine the reset cause
 *
 * @param[in] rstFlags STM32 RCC reset flags captured before they are cleared.
 * @return Persistent reset cause used by the state machine.
 */
t_resetCause getResetCause(uint32_t rstFlags)
{
  t_resetCause resetCause = resetException; // default case

  // note that the reset flags are cleared only  after we've processed
  // things.  That way, bad things are sticky

  do
  {
    // if the backup registers aren't valid, it doesn't
    // matter how we got here, it's treated as a power
    // on reset

    if (pState->valid == false)
    {
      resetCause = resetPower; 
      break;
    }

    // Brownout leaves memory in questionable state
    // but shutdown also causes brownout
    // can't reliably detect shutdown !

    if ((rstFlags & RCC_CSR_BORRSTF))
    {
      resetCause = resetBrownout; // unplanned brownout
      break;
    }

    // Standby is marked, but reset is also ok in safe mode
    // (safe mode marked in backup registers

    if ((PWR->SR1 & PWR_SR1_SBF) || pState->safe)
    {
      resetCause = resetStandby;
      PWR->SCR = PWR_SCR_CSBF;
      break;
    }

    // "pin" reset

    if ((rstFlags & RCC_CSR_PINRSTF))
    {
      resetCause = resetException;
      break;
    }
  } while (0);

  return resetCause;
}
/** @} */

/** @name Event polling
 * Event helpers translate hardware wake flags into ChibiOS event bits consumed
 * by the state machine.
 * @{
 */
/**
 * @brief Clear wake flags and signal pending RTC events to the main thread.
 */
void CheckEvents(void)
{
  // clear wakeup flag, then check status
  // we may get an extra wakeup, but we won't miss
  // an interrupt

  WRITE_REG(PWR->SCR, PWR_SCR_CWUF);

  // check and clear RTC alarms

  uint32_t clear = (0U
           | RTC_ISR_TSF
           | RTC_ISR_TSOVF
#if defined(RTC_ISR_TAMP1F)
           | RTC_ISR_TAMP1F
#endif
#if defined(RTC_ISR_TAMP2F)
           | RTC_ISR_TAMP2F
#endif
#if defined(RTC_ISR_TAMP3F)
           | RTC_ISR_TAMP3F
#endif
#if defined(RTC_ISR_WUTF)
           | RTC_ISR_WUTF
#endif
#if defined(RTC_ISR_ALRAF)
           | RTC_ISR_ALRAF
#endif
#if defined(RTC_ISR_ALRBF)
           | RTC_ISR_ALRBF
#endif
          );

// clear only those alarms that are set

  uint32_t isr = RTCD1.rtc->ISR;
  RTCD1.rtc->ISR = (isr & ~clear);  

// pass set alarms to main thread as events

  chEvtSignal(tpMain, EVT_RTC_MASK(isr));
}
/** @} */

uint32_t start_cycles;

/** @name Firmware entry points
 * Top-level entry points connect ChibiOS startup, reset handling, the state
 * machine, and low-power entry.
 * @{
 */
/**
 * @brief Initialize the tag runtime and repeatedly run event/state processing.
 *
 * @return Never returns during normal firmware execution.
 */
int main(void)
{
  // read the reset flags

  uint32_t rstFlags = RCC->CSR;
  
  halInit();
  chSysInit();
  i2cObjectInit(&I2CD1);

 // low power run mode
 // PWR->CR1 |= PWR_CR1_LPR_Msk;
  
  chBSemObjectInit(&ADCmutex, false);
  chBSemObjectInit(&I2Cmutex, false);
  chBSemObjectInit(&SPImutex, false);
  chBSemObjectInit(&USARTmutex, false);
  
  tpMain = chThdGetSelfX(); // global pointer to main thread

  // save reset cause

  pState->resetCause = getResetCause(rstFlags);

   // clear reset flags -- we've done our job at this point
  RCC->CSR |= RCC_CSR_RMVF;

  // Initialize system (if power on)

  deviceInit(false);

  // Configure RTC to bypass shadow registers

  RTCD1.rtc->CR |= RTC_CR_BYPSHAD;

  // clear deep sleep mask

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  while (1)
  {
    enum Sleep sleepmode = STANDBY;
    timestamp = GetTimeUnixSec(&timestamp_millis); // get current time
    pState->safe = false;                          // critical section start
    
    CheckEvents();                                 // see if adxl or rtc have events
    sleepmode = StateMachine();                    // process events

    // critical section end

    pState->safe = true;

    if (MONCONNECTED)
    {
      chThdYield(); // if the monitor is connected, don't sleep
    }
    else
    {
      godown(sleepmode); // standby
    }
  }
}

/**
 * @brief ChibiOS/vector-table reset entry used as the exception recovery target.
 */
extern void Reset_Handler(void);

/**
 * @brief Route unexpected exceptions through the reset handler.
 */
void __unhandled_exception(void) { Reset_Handler(); }
/** @} */
