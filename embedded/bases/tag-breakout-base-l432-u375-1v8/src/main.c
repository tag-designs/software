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
 * @brief   USB SWD programmer firmware for the L432/U375 1.8 V breakout base.
 *
 * @details Initializes USB CDC/bulk endpoints, bitbang/SPI-assisted SWD
 *          control, target reset/direction GPIOs, and the local green status
 *          LED. The status LED flashes only while the STLink debug session is
 *          open. It uses TIM2 channel 4 as a high-frequency PWM brightness
 *          carrier, while a ChibiOS virtual timer gates the channel on and
 *          off for the visible blink envelope.
 */

#include "ch.h"
#include "hal.h"
#include "usbcfg.h"
#include "app.h"
#include "board.h"
#include "dp_swd.h"

extern SerialUSBDriver SDU1;
extern const SerialUSBConfig serusbcfg;
extern bool stlink_open;

volatile uint32_t vlipo100 = 180;

uint8_t bulkbuf[64];

/**
 * @def     LED_GREEN_PWM_FREQUENCY_HZ
 * @brief   TIM2 PWM timer clock used for the green LED brightness carrier.
 */
#define LED_GREEN_PWM_FREQUENCY_HZ 1000000U

/**
 * @def     LED_GREEN_PWM_PERIOD_TICKS
 * @brief   PWM carrier period in TIM2 ticks.
 * @details Combined with @ref LED_GREEN_PWM_FREQUENCY_HZ, this produces a
 *          1 kHz carrier that controls apparent LED brightness without
 *          visible flicker.
 */
#define LED_GREEN_PWM_PERIOD_TICKS 1000U

/**
 * @def     LED_GREEN_PWM_DUTY_PERCENT
 * @brief   Green LED brightness duty while the blink gate is enabled.
 * @details ChibiOS @c PWM_PERCENTAGE_TO_WIDTH() uses a 0..10000 scale, so
 *          1000U represents 10% carrier duty.
 */
#define LED_GREEN_PWM_DUTY_PERCENT 1000U

/**
 * @def     LED_GREEN_BLINK_ON_MS
 * @brief   Visible green LED blink on-time in milliseconds.
 */
#define LED_GREEN_BLINK_ON_MS 250U

/**
 * @def     LED_GREEN_BLINK_OFF_MS
 * @brief   Visible green LED blink off-time in milliseconds.
 */
#define LED_GREEN_BLINK_OFF_MS 750U

/**
 * @def     LED_GREEN_STLINK_CLOSED_POLL_MS
 * @brief   Timer polling interval while no STLink session is open.
 */
#define LED_GREEN_STLINK_CLOSED_POLL_MS 100U

static virtual_timer_t led_green_blink_timer; ///< One-shot timer that gates the visible LED blink.
static bool led_green_blink_enabled = false;  ///< True while TIM2 channel 4 is enabled.

/**
 * @brief   TIM2 PWM configuration for the green status LED carrier.
 * @details PA3 is routed to TIM2 channel 4 using AF1 in the generated board
 *          configuration. Channels 1-3 are disabled because this target uses
 *          TIM2 only for the green LED.
 */
static const PWMConfig led_green_pwm_config = {
  LED_GREEN_PWM_FREQUENCY_HZ,
  LED_GREEN_PWM_PERIOD_TICKS,
  NULL,
  {
    {PWM_OUTPUT_DISABLED, NULL},
    {PWM_OUTPUT_DISABLED, NULL},
    {PWM_OUTPUT_DISABLED, NULL},
    {PWM_OUTPUT_ACTIVE_HIGH, NULL}
  },
  0,
  0,
  0
};

/**
 * @brief   Enables or disables the green LED PWM carrier from locked context.
 *
 * @param[in] enabled   true to drive TIM2 channel 4 at the configured
 *                      brightness duty, false to force the LED off.
 *
 * @pre     TIM2 PWM must have been started with @c pwmStart().
 * @pre     Caller must hold the ChibiOS system lock.
 * @post    @ref led_green_blink_enabled matches @p enabled.
 */
static void ledGreenSetPwmI(bool enabled)
{
  if (enabled)
  {
    pwmEnableChannelI(&PWMD2, 3,
                      PWM_PERCENTAGE_TO_WIDTH(&PWMD2,
                                              LED_GREEN_PWM_DUTY_PERCENT));
  }
  else
  {
    pwmDisableChannelI(&PWMD2, 3);
  }
  led_green_blink_enabled = enabled;
}

/**
 * @brief   Toggles the visible green LED blink gate.
 *
 * @details Runs from the ChibiOS virtual timer callback path. The callback
 *          keeps TIM2 channel 4 disabled while @c stlink_open is false. Once
 *          the STLink session is open, it alternates between disabling the
 *          channel for the off interval and re-enabling it at
 *          @ref LED_GREEN_PWM_DUTY_PERCENT for the on interval. The PWM
 *          carrier keeps the enabled LED dim; the virtual timer controls only
 *          the human-visible blink envelope.
 *
 * @param[in] vtp   Virtual timer instance that fired. The callback rearms the
 *                  module-global timer and does not otherwise use @p vtp.
 * @param[in] arg   Unused callback argument.
 *
 * @pre     @ref led_green_blink_timer must have been initialized with
 *          @c chVTObjectInit().
 * @post    The timer is rearmed for the next on/off interval.
 *
 * @warning Runs from timer callback context and uses ISR-class ChibiOS/PWM
 *          APIs under @c chSysLockFromISR().
 */
static void ledGreenBlinkTimer(virtual_timer_t *vtp, void *arg)
{
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  if (!stlink_open)
  {
    ledGreenSetPwmI(false);
    chVTSetI(&led_green_blink_timer,
             TIME_MS2I(LED_GREEN_STLINK_CLOSED_POLL_MS),
             ledGreenBlinkTimer, NULL);
    chSysUnlockFromISR();
    return;
  }

  if (led_green_blink_enabled)
  {
    ledGreenSetPwmI(false);
    chVTSetI(&led_green_blink_timer, TIME_MS2I(LED_GREEN_BLINK_OFF_MS),
             ledGreenBlinkTimer, NULL);
  }
  else
  {
    ledGreenSetPwmI(true);
    chVTSetI(&led_green_blink_timer, TIME_MS2I(LED_GREEN_BLINK_ON_MS),
             ledGreenBlinkTimer, NULL);
  }
  chSysUnlockFromISR();
}

/**
 * @brief   Starts the dimmed green status LED blink.
 *
 * @details Starts TIM2 PWM, leaves channel 4 off until @c stlink_open is
 *          true, initializes the virtual timer used for the visible blink
 *          envelope, and arms the closed-session poll interval. Must be called
 *          after @c chSysInit() so the PWM driver and virtual timer services
 *          are available.
 *
 * @pre     Board initialization must have configured PA3 as TIM2_CH4 AF1.
 * @post    The green LED timer is armed; the LED remains off until
 *          @c stlink_open becomes true.
 */
static void ledGreenStart(void)
{
  pwmStart(&PWMD2, &led_green_pwm_config);
  chVTObjectInit(&led_green_blink_timer);
  pwmDisableChannel(&PWMD2, 3);
  chVTSet(&led_green_blink_timer,
          TIME_MS2I(LED_GREEN_STLINK_CLOSED_POLL_MS),
          ledGreenBlinkTimer, NULL);
}

/**
 * @brief   Starts USB CRS autotrim against the USB SOF reference.
 *
 * @details Enables the STM32 CRS peripheral and configures it to trim HSI48
 *          from the USB synchronization source. This keeps the USB clock
 *          within tolerance without an external HSE crystal.
 *
 * @pre     HSI48 must be enabled by the clock configuration.
 * @post    CRS automatic trim and frequency error counter are enabled.
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

  palClearLine(LINE_SWDIO_DIR);
  palClearLine(LINE_TGT_RESET);
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

  ledGreenStart();

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
