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

#ifndef TAG_STM32U3_FLASH
#define TAG_STM32U3_FLASH 0
#endif

#ifndef TAG_MONITOR_RESET_RECOVERY
#define TAG_MONITOR_RESET_RECOVERY 0
#endif

/** @name Shared runtime state
 * Timestamps and main-thread handle shared by runtime services.
 * @{
 */
thread_t *tpMain = 0;
int32_t timestamp;
uint32_t timestamp_millis;
bool rtcInitializedAtBoot;

#if defined(TAMP_BKP0R) && !defined(RTC_BKP0R)
#define TAG_BACKUP_STATE_REG0 (&TAMP->BKP0R)
#else
#define TAG_BACKUP_STATE_REG0 (&RTC->BKP0R)
#endif

volatile BackupState *const pState = (BackupState *)TAG_BACKUP_STATE_REG0;
/** @} */

static inline bool tagPowerStandbyFlagSet(void)
{
#if defined(PWR_SR1_SBF)
  return (PWR->SR1 & PWR_SR1_SBF) != 0;
#elif defined(PWR_SR_SBF)
  return (PWR->SR & PWR_SR_SBF) != 0;
#else
  return false;
#endif
}

static inline void tagPowerClearStandbyFlag(void)
{
#if defined(PWR_SCR_CSBF)
  PWR->SCR = PWR_SCR_CSBF;
#elif defined(PWR_SR_CSSF)
  PWR->SR = PWR_SR_CSSF;
#endif
}

static inline void tagPowerClearWakeFlags(void)
{
#if defined(PWR_SCR_CWUF)
  WRITE_REG(PWR->SCR, PWR_SCR_CWUF);
#elif defined(PWR_WUSCR_CWUF1)
  uint32_t clear = 0U;
#if defined(PWR_WUSCR_CWUF1)
  clear |= PWR_WUSCR_CWUF1;
#endif
#if defined(PWR_WUSCR_CWUF2)
  clear |= PWR_WUSCR_CWUF2;
#endif
#if defined(PWR_WUSCR_CWUF3)
  clear |= PWR_WUSCR_CWUF3;
#endif
#if defined(PWR_WUSCR_CWUF4)
  clear |= PWR_WUSCR_CWUF4;
#endif
#if defined(PWR_WUSCR_CWUF5)
  clear |= PWR_WUSCR_CWUF5;
#endif
#if defined(PWR_WUSCR_CWUF6)
  clear |= PWR_WUSCR_CWUF6;
#endif
#if defined(PWR_WUSCR_CWUF7)
  clear |= PWR_WUSCR_CWUF7;
#endif
#if defined(PWR_WUSCR_CWUF8)
  clear |= PWR_WUSCR_CWUF8;
#endif
#if defined(PWR_WUSCR_CWUF9)
  clear |= PWR_WUSCR_CWUF9;
#endif
#if defined(PWR_WUSCR_CWUF10)
  clear |= PWR_WUSCR_CWUF10;
#endif
  WRITE_REG(PWR->WUSCR, clear);
#endif
}

static inline bool tagRtcInitialized(void)
{
#if defined(RTC_ISR_INITS)
  return (RTC->ISR & RTC_ISR_INITS) != 0;
#elif defined(RTC_ICSR_INITS)
  return (RTC->ICSR & RTC_ICSR_INITS) != 0;
#else
  return false;
#endif
}

static inline void tagPowerReleaseStandbyPulls(void)
{
#if defined(PWR_CR3_APC)
  CLEAR_BIT(PWR->CR3, PWR_CR3_APC);
#elif defined(PWR_APCR_APC)
  CLEAR_BIT(PWR->APCR, PWR_APCR_APC);
#endif
}

static eventmask_t tagRtcEventsAndClear(void)
{
#if defined(RTC_ISR_TSF)
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

  uint32_t isr = RTCD1.rtc->ISR;
  RTCD1.rtc->ISR = (isr & ~clear);
  return EVT_RTC_MASK(isr);
#elif defined(RTC_SR_TSF) && defined(RTC_SCR_CTSF)
  uint32_t sr = RTCD1.rtc->SR;
  uint32_t clear = 0U;
  eventmask_t events = 0U;

#if defined(RTC_SR_TSF) && defined(RTC_SCR_CTSF)
  if ((sr & RTC_SR_TSF) != 0)
    clear |= RTC_SCR_CTSF;
#endif
#if defined(RTC_SR_TSOVF) && defined(RTC_SCR_CTSOVF)
  if ((sr & RTC_SR_TSOVF) != 0)
    clear |= RTC_SCR_CTSOVF;
#endif
#if defined(RTC_SR_ALRAF) && defined(RTC_SCR_CALRAF)
  if ((sr & RTC_SR_ALRAF) != 0) {
    clear |= RTC_SCR_CALRAF;
    events |= EVT_RTC_ALRAF;
  }
#endif
#if defined(RTC_SR_ALRBF) && defined(RTC_SCR_CALRBF)
  if ((sr & RTC_SR_ALRBF) != 0) {
    clear |= RTC_SCR_CALRBF;
    events |= EVT_RTC_ALRBF;
  }
#endif
#if defined(RTC_SR_WUTF) && defined(RTC_SCR_CWUTF)
  if ((sr & RTC_SR_WUTF) != 0) {
    clear |= RTC_SCR_CWUTF;
    events |= EVT_RTC_WUTF;
  }
#endif

  if (clear != 0U)
    RTCD1.rtc->SCR = clear;

  return events;
#else
  return 0U;
#endif
}

void tagSystemInitHook(void)
{
  NVIC_SetPriority(DebugMonitor_IRQn, TAG_DEBUG_MONITOR_PRIORITY);
}

#if TAG_STM32U3_FLASH
static void tagBackupStateEnableWrites(void)
{
#if defined(RCC_APB1ENR1_RTCAPBEN)
  RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN;
#endif
#if defined(PWR_DBPR_DBP)
  PWR->DBPR |= PWR_DBPR_DBP;
#elif defined(PWR_CR1_DBP)
  PWR->CR1 |= PWR_CR1_DBP;
#endif
}
#endif

#if TAG_MONITOR_RESET_RECOVERY
static void tagResetRuntimeStateForPowerInit(void)
{
  pState->state = TagState_IDLE;
  pState->pages = 0;
  pState->external_blocks = 0;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  pState->run_heartbeat = 0;
  pState->terminal_state = STATE_UNSPECIFIED;
  pState->terminal_reason = State_EVENT_UNSPECIFIED;
  pState->header_status = LOGWRITE_OK;
  pState->header_flasherr = 0;
  pState->header_page = 0;
  pState->header_addr = 0;
  pState->header_retries = 0;
#endif
  pState->test_result = TEST_UNSPECIFIED;
}
#endif

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
#if TAG_MONITOR_RESET_RECOVERY
  // Monitor attach resets can arrive through the power-init path while backup
  // state is still valid; preserve persistent fields such as test_result then.
  bool retained_state_valid = pState->valid == BACKUP_STATE_VALID_MAGIC;
  bool monitor_reset_recovery =
      retained_state_valid &&
      (MONCONNECTED ||
       ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U));

  if ((power_init && !monitor_reset_recovery) || force)
  {
    // make sure everything is zeroed

    // Configure the external RTC only for true power initialization. Forced
    // cleanup runs under monitor control and should avoid unnecessary I2C work.

    if (power_init)
      tagRtcInit();

    pState->valid = 0;
    pState->safe = false;
    if (power_init && !retained_state_valid)
      tagResetRuntimeStateForPowerInit();
#else
  if (power_init || force)
  {
    // make sure everything is zeroed

    pState->valid = 0;
    pState->safe = false;

    // Configure the external RTC only for true power initialization. Forced
    // cleanup runs under monitor control and should avoid unnecessary I2C work.

    if (power_init)
      tagRtcInit();
#endif

    TagDevicePowerReason power_reason = TAG_DEVICE_POWER_RUNTIME_DEINIT;
    if (power_init) {
      power_reason = TAG_DEVICE_POWER_BOOT_CLEANUP;
    } else if ((pState->state == TagState_FINISHED) ||
               (pState->state == TagState_ABORTED)) {
      power_reason = TAG_DEVICE_POWER_TERMINAL_ENTRY;
    }
    tagDevicesApplyPowerState(power_reason, pState->state);


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

     if (!tagPowerStandbyFlagSet())
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

    if (tagPowerStandbyFlagSet() || pState->safe)
    {
      resetCause = resetStandby;
      // clear the standby bit
      tagPowerClearStandbyFlag();
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

  tagPowerClearWakeFlags();

  // check and clear RTC alarms

  return tagRtcEventsAndClear();
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
#if TAG_STM32U3_FLASH
  rtcInitializedAtBoot = tagRtcInitialized();
#else
  rtcInitializedAtBoot = (RTC->ISR & RTC_ISR_INITS) != 0;
#endif

  halInit();
  chSysInit();

  // release the standby pullup/pulldown

#if TAG_STM32U3_FLASH
  tagPowerReleaseStandbyPulls();
#else
  CLEAR_BIT(PWR->CR3, PWR_CR3_APC);
#endif
  tagClearStandbyPulls();

  // low power run mode
  // PWR->CR1 |= PWR_CR1_LPR_Msk;

  adcInit();
  debug_log_init();
  tagPowerInit();
#if TAG_STM32U3_FLASH
  tagBackupStateEnableWrites();
#endif
  tagDevicesInit();

  tpMain = chThdGetSelfX(); // global pointer to main thread

  // save reset cause

  pState->resetCause = getResetCause(rstFlags);

   // clear reset flags -- we've done our job at this point
  RCC->CSR |= RCC_CSR_RMVF;

  // Initialize system (if power on)

  deviceInit(false);

  // Configure RTC to bypass shadow registers when the calendar is live.

#if TAG_STM32U3_FLASH
  if (tagRtcInitialized())
  {
    RTCD1.rtc->CR |= RTC_CR_BYPSHAD;
  }
#else
  RTCD1.rtc->CR |= RTC_CR_BYPSHAD;
#endif

  // clear deep sleep mask

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  // Release swdio and swclk if not in monitor mode

  if (0 && !MONCONNECTED)
  {
    palSetLineMode(LINE_SWDIO, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(LINE_SWCLK, PAL_MODE_INPUT_ANALOG);
  }


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
