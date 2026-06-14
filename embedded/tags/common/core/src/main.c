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
#include "debug_log.h"
#include "device.h"
#include "monitor.h"
#include "persistent.h"
#include "power.h"
#include "rtc_api.h"
#include "timekeeping.h"

#include "config.h"

#ifndef TAG_DEBUG_MONITOR_PRIORITY
#define TAG_DEBUG_MONITOR_PRIORITY 8U
#endif

#ifndef BACKUP_STATE_VALID_MAGIC
#define BACKUP_STATE_VALID_MAGIC 1U
#endif

/** @name Shared runtime state
 * Timestamps and main-thread handle shared by runtime services.
 * @{
 */
thread_t *tpMain = 0;
int32_t timestamp;
uint32_t timestamp_millis;
bool rtcInitializedAtBoot;

volatile BackupState *const pState = (BackupState *)&RTC->BKP0R;
/** @} */

void tagSystemInitHook(void)
{
  NVIC_SetPriority(DebugMonitor_IRQn, TAG_DEBUG_MONITOR_PRIORITY);
}

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

  bool power_init = (pState->resetCause == resetPower) ||
                    (pState->resetCause == resetBrownout);

  if (power_init || force)
  {
    // make sure everything is zeroed

    pState->valid = 0;
    pState->safe = false;

    // Configure the external RTC only for true power initialization. Forced
    // cleanup runs under monitor control and should avoid unnecessary I2C work.

    if (power_init)
      tagRtcInit();

    TagDevicePowerReason power_reason = TAG_DEVICE_POWER_RUNTIME_DEINIT;
    if (power_init) {
      power_reason = TAG_DEVICE_POWER_BOOT_CLEANUP;
    } else if ((pState->state == TagState_FINISHED) ||
               (pState->state == TagState_ABORTED)) {
      power_reason = TAG_DEVICE_POWER_TERMINAL_ENTRY;
    }
    tagDevicesApplyPowerState(power_reason, pState->state);
    for (int i = 0; i < 16; i++) {
      debug_log_printf("Pin A%d 0x%02x B%d 0x%02x\r\n", i,GPIOA->MODER>>(i*2)&3, i, GPIOB->MODER>>(i*2)&3);
    }


    // turn off all alarms

    disableAllAlarms();
    disableTicker();
    pState->valid = BACKUP_STATE_VALID_MAGIC;

    // is this the right place to set pullup/pulldowns?
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

    if (pState->valid != BACKUP_STATE_VALID_MAGIC)
    {
      resetCause = resetPower; 
      break;
    }

    // A firmware-triggered reset is used after unhandled exceptions.
    // It can happen while the tag is active, so it will not have SBF set.
    if ((rstFlags & RCC_CSR_SFTRSTF))
    {
      resetCause = resetException;
      break;
    }

    // not an exit from standby, so must be power on

     if (!(PWR->SR1 & PWR_SR1_SBF))
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
      // clear the standby bit
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
eventmask_t CheckEvents(void)
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

// return set alarms as events

  return EVT_RTC_MASK(isr);
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
  rtcInitializedAtBoot = (RTC->ISR & RTC_ISR_INITS) != 0;

  halInit();
  chSysInit();

  // release the standby pullup/pulldown

  CLEAR_BIT(PWR->CR3, PWR_CR3_APC);

  // low power run mode
  // PWR->CR1 |= PWR_CR1_LPR_Msk;

  adcInit();
  debug_log_init();
  tagPowerInit();
  tagDevicesInit();
  
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
    eventmask_t pending_events;
    timestamp = GetTimeUnixSec(&timestamp_millis); // get current time
    pState->safe = false;                          // critical section start

    pending_events = CheckEvents();               // see if adxl or rtc have events
    pending_events |= chEvtWaitAnyTimeout(ALL_EVENTS, TIME_IMMEDIATE);
    if ((pending_events & EVT_MONITOR_ALL) || monitorNeedsService())
    {
      pending_events |= monitorServicePending((uint32_t)pending_events);
    }
    sleepmode = StateMachine(pending_events);      // process events

    // critical section end

    pState->safe = true;

    // godown

    godown(sleepmode); // standby
  }
}

/** @} */
