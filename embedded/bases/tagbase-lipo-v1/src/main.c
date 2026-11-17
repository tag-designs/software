/**************************************************************************
*      Copyright 2018  Geoffrey Brown                                     *
*                                                                         *
* Licensed under the Apache License, Version 2.0 (the "License");         *
* you may not use this file except in compliance with the License.        *
* You may obtain a copy of the License at                                 *
*                                                                         *
*     http://www.apache.org/licenses/LICENSE-2.0                          *
*                                                                         *
* Unless required by applicable law or agreed to in writing, software     *
* distributed under the License is distributed on an "AS IS" BASIS,       *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
* See the License for the specific language governing permissions and     *
* limitations under the License.                                          *
**************************************************************************/

/**
 * @file    main.c
 * @brief   USB SWD programmer firmware for the LiPo Base V1.
 *
 * @details The LiPo Base V1 (hardware @c tagbase-lipo-v1) programs a tag over
 *          SWD and charges the tag's LiPo cell from USB through an XC6808
 *          linear charger. Its STM32L432 runs the same ST-LINK emulation and
 *          SPI-assisted SWD as @c tag-breakout-base-l432-u375-1v8 (shared from
 *          @c common/l432). This file adds what the board does differently:
 *
 *          - the 1.8 V level-shifter supply (EN1V8, PB6) follows the SWD
 *            session, through tgtIoPowerOn() and tgtIoPowerOff(), which
 *            @c stlink.c calls via @c base_board.h;
 *          - tag reset is an open-drain, active-low line (see
 *            @c base_board.h);
 *          - two LEDs on TIM2: green (PA2, channel 3) blinks while an SWD
 *            session is open, as on the breakout; red (PA3, channel 4) shows
 *            the charger's CSO output on PA1.
 *
 *          The base reports a fixed 1.80 V target voltage. PA0 carries a
 *          TLV9001 follower of TAG_3V3, but its input floats when nothing
 *          drives TAG_3V3, so the reading is not used.
 */

#include "ch.h"
#include "hal.h"
#include "usbcfg.h"
#include "app.h"
#include "board.h"
#include "dp_swd.h"
#include "base_board.h"

extern SerialUSBDriver SDU1;
extern const SerialUSBConfig serusbcfg;
extern bool stlink_open;

/**
 * @brief   Target voltage reported by STLINK_GET_TARGET_VOLTAGE, in 10 mV.
 * @details Fixed at the tag's 1.8 V I/O level; see the file note for why the
 *          PA0 sense is not used.
 */
volatile uint32_t vlipo100 = 180;

uint8_t bulkbuf[64]; ///< One ST-LINK command from the host.

/**
 * @def     EN1V8_SETTLE_MS
 * @brief   Wait after enabling the AP2112 before the level shifters are used.
 * @details Covers the regulator's start-up into C5 (1 uF) plus the shifter
 *          decoupling, with margin. Paid once per session, not per command.
 */
#define EN1V8_SETTLE_MS 2U

/**
 * @def     LED_PWM_FREQUENCY_HZ
 * @brief   TIM2 PWM timer clock used for the LED brightness carrier.
 */
#define LED_PWM_FREQUENCY_HZ 1000000U

/**
 * @def     LED_PWM_PERIOD_TICKS
 * @brief   PWM carrier period in TIM2 ticks: a 1 kHz carrier, no flicker.
 */
#define LED_PWM_PERIOD_TICKS 1000U

/**
 * @def     LED_GREEN_PWM_DUTY_PERCENT
 * @brief   Green LED brightness while lit, on ChibiOS's 0..10000 scale
 *          (1000 = 10 %).
 */
#define LED_GREEN_PWM_DUTY_PERCENT 1000U

/**
 * @def     LED_RED_PWM_DUTY_PERCENT
 * @brief   Red LED brightness while lit, on ChibiOS's 0..10000 scale.
 */
#define LED_RED_PWM_DUTY_PERCENT 1000U

/** @brief TIM2 channel index (0-based) driving the green LED on PA2. */
#define LED_GREEN_CHANNEL 2U

/** @brief TIM2 channel index (0-based) driving the red LED on PA3. */
#define LED_RED_CHANNEL 3U

/**
 * @def     STATUS_PERIOD_MS
 * @brief   Period of the status thread, and the unit of the blink timings.
 */
#define STATUS_PERIOD_MS 50U

/** @brief Green on-time while a session is open, in status periods (250 ms). */
#define LED_GREEN_ON_PERIODS 5U

/** @brief Green blink period while a session is open, in status periods (1 s). */
#define LED_GREEN_CYCLE_PERIODS 20U

/** @brief Red blink half-period on a charger fault, in status periods (100 ms). */
#define LED_RED_FAULT_PERIODS 2U

/**
 * @def     CSO_SAMPLES
 * @brief   CSO samples taken per status period, one system tick (100 us)
 *          apart.
 * @details The XC6808A signals a fault by toggling CSO at 0.75 to 1.25 kHz.
 *          40 samples span about 4 ms, at least three periods of the
 *          slowest toggle, and each half-period (at least 400 us) holds
 *          several samples, so a toggling CSO always shows both levels.
 */
#define CSO_SAMPLES 40U

/**
 * @enum    charge_state_t
 * @brief   Charger state decoded from the XC6808 CSO pin.
 */
typedef enum {
  CHARGE_IDLE,     ///< CSO off (high): charge complete, or no cell on BAT.
  CHARGE_ACTIVE,   ///< CSO on (low): charging.
  CHARGE_FAULT     ///< CSO toggling at ~1 kHz: timer, voltage or thermal fault.
} charge_state_t;

static bool en1v8_on = false; ///< True while EN1V8 is set; main thread only.

static THD_WORKING_AREA(waStatus, 256); ///< Status (LED) thread stack.

/**
 * @brief   TIM2 PWM configuration for both LED carriers.
 * @details PA2 and PA3 are TIM2 channels 3 and 4 on AF1 in the generated
 *          board file. Channels 1 and 2 are unused.
 */
static const PWMConfig led_pwm_config = {
  LED_PWM_FREQUENCY_HZ,
  LED_PWM_PERIOD_TICKS,
  NULL,
  {
    {PWM_OUTPUT_DISABLED, NULL},
    {PWM_OUTPUT_DISABLED, NULL},
    {PWM_OUTPUT_ACTIVE_HIGH, NULL},
    {PWM_OUTPUT_ACTIVE_HIGH, NULL}
  },
  0,
  0,
  0
};

void tgtIoPowerOn(void)
{
  if (en1v8_on)
    return;
  palSetLine(LINE_EN1V8);
  en1v8_on = true;
  chThdSleepMilliseconds(EN1V8_SETTLE_MS);
}

void tgtIoPowerOff(void)
{
  palClearLine(LINE_EN1V8);
  en1v8_on = false;
}

/**
 * @brief   Lights or darkens one LED.
 *
 * @param[in] channel   TIM2 channel index, LED_GREEN_CHANNEL or
 *                      LED_RED_CHANNEL.
 * @param[in] duty      Brightness on the 0..10000 scale when @p on.
 * @param[in] on        true to light the LED, false to turn it off.
 *
 * @pre     TIM2 PWM has been started with @c pwmStart().
 */
static void ledSet(pwmchannel_t channel, uint32_t duty, bool on)
{
  if (on)
    pwmEnableChannel(&PWMD2, channel, PWM_PERCENTAGE_TO_WIDTH(&PWMD2, duty));
  else
    pwmDisableChannel(&PWMD2, channel);
}

/**
 * @brief   Samples CSO for about 4 ms and decodes the charger state.
 *
 * @details CSO is open-drain, pulled up to 3.3 V by R3. A window that is all
 *          low is charging, all high is idle, and both levels is the fault
 *          toggle. A real change of state inside a window also shows both
 *          levels, so the caller confirms a fault over two windows.
 *
 * @return  The state seen in this window.
 *
 * @warning Sleeps for CSO_SAMPLES system ticks.
 */
static charge_state_t csoSample(void)
{
  unsigned low = 0;
  for (unsigned i = 0; i < CSO_SAMPLES; i++)
  {
    if (palReadLine(LINE_CHG_STATUS) == PAL_LOW)
      low++;
    chThdSleep(1);
  }
  if (low == 0)
    return CHARGE_IDLE;
  if (low == CSO_SAMPLES)
    return CHARGE_ACTIVE;
  return CHARGE_FAULT;
}

/**
 * @brief   Drives both LEDs.
 *
 * @details Every STATUS_PERIOD_MS: the green LED blinks (250 ms on, 750 ms
 *          off) while @c stlink_open is true and is off otherwise. The red LED
 *          is on while charging, off when idle, and blinks at 2.5 Hz once two
 *          consecutive CSO windows show the fault toggle.
 *
 * @param[in] arg   Unused.
 */
static THD_FUNCTION(statusThread, arg)
{
  (void)arg;
  unsigned tick = 0;
  unsigned fault_windows = 0;

  chRegSetThreadName("status");
  pwmStart(&PWMD2, &led_pwm_config);
  ledSet(LED_GREEN_CHANNEL, LED_GREEN_PWM_DUTY_PERCENT, false);
  ledSet(LED_RED_CHANNEL, LED_RED_PWM_DUTY_PERCENT, false);

  systime_t next = chVTGetSystemTime();
  while (true)
  {
    tick++;

    bool green = stlink_open &&
                 (tick % LED_GREEN_CYCLE_PERIODS) < LED_GREEN_ON_PERIODS;
    ledSet(LED_GREEN_CHANNEL, LED_GREEN_PWM_DUTY_PERCENT, green);

    charge_state_t charge = csoSample();
    fault_windows = (charge == CHARGE_FAULT) ? fault_windows + 1 : 0;
    bool red;
    if (fault_windows >= 2)
      red = ((tick / LED_RED_FAULT_PERIODS) & 1U) != 0;
    else
      red = (charge == CHARGE_ACTIVE);
    ledSet(LED_RED_CHANNEL, LED_RED_PWM_DUTY_PERCENT, red);

    next = chThdSleepUntilWindowed(next, chTimeAddX(next,
                                   TIME_MS2I(STATUS_PERIOD_MS)));
  }
}

/**
 * @brief   Starts USB CRS autotrim against the USB SOF reference.
 *
 * @details Trims HSI48 from USB start-of-frame packets so the USB clock stays
 *          within tolerance without a crystal.
 *
 * @pre     HSI48 is enabled by the clock configuration.
 * @post    CRS automatic trim and the frequency error counter are enabled.
 */
static void crsStart(void)
{
  RCC->APB1ENR1 |= RCC_APB1ENR1_CRSEN;

  CRS->CFGR = (2U << CRS_CFGR_SYNCSRC_Pos) |
              (34U << CRS_CFGR_FELIM_Pos) |
              48000U;
  CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;
}

int main(void)
{
  halInit();

  crsStart();

  tgtIoPowerOff();
  TGT_RESET_RELEASE();
  palClearLine(LINE_SWDIO_DIR);
  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);
  palClearLine(LINE_UART_TX);
  toInput(LINE_TGT_SWDIO_IN);

  chSysInit();

  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);

  usbDisconnectBus(&USBD1);
  chThdSleepMilliseconds(1500);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(&USBD1);

  // Above the ST-LINK loop so the CSO sampling keeps its spacing during
  // long SWD transfers; it wakes for one tick at a time.
  chThdCreateStatic(waStatus, sizeof(waStatus), NORMALPRIO + 1,
                    statusThread, NULL);

  while (true)
  {
    int n = BULK_Receive(bulkbuf, 64);
    if (n != 16)
    {
      chThdSleepMilliseconds(10);
    }
    else if (usbGetDriverStateI(&USBD1) == USB_ACTIVE)
    {
      palSetLine(LINE_UART_TX);
      stlink_eval(bulkbuf);
      palClearLine(LINE_UART_TX);
    }
  }
}
