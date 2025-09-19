#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "ADXL362.h"
#include "ADXL367.h"
#include "config.h"
#include "persistent.h"
#include "lis2du12.h"

// synchronization variables

binary_semaphore_t ADCmutex;
binary_semaphore_t SPImutex;
binary_semaphore_t I2Cmutex;

// main thread

thread_t *tpMain = 0;
int32_t timestamp;
uint32_t timestamp_millis;

volatile BackupState *const pState = (BackupState *)&RTC->BKP0R;

/**********************************************************
 *  Voltage and Temperature Measurement
 **********************************************************/

#define ADC_SMPR_SMP_47P5 4

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

// Perform basic hardware initialization if the backup registers
// have been zeroed (due to power on), or if "forced."  Currently
// this is test code (timer with alarm, accelerometer continuously
// sampling

//extern void disableSecondsAlarm(void);
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

    initRTC();

    // Reset Accelerometer

#if defined(USE_ADXL362)
    accelSpiOn();
    ADXL362_SoftwareReset();
    chThdSleepMilliseconds(2);
    accelSpiOff();
#endif

#if defined(USE_ADXL367)
    accelSpiOn();
    ADXL367_SoftwareReset();
    chThdSleepMilliseconds(2);
    accelSpiOff();
#endif

#if defined(USE_LIS2DU12)
    accelDeinit();
#endif

    // turn off all alarms

    disableAllAlarms();
    disableTicker();
    pState->valid = true;
  }
}

/*
 * Because we use standby mode and because attaching the monitor may
 * trigger a reset, it is important to determine the reset cause
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

uint32_t start_cycles;
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

extern void Reset_Handler(void);
void __unhandled_exception(void) { Reset_Handler(); }
