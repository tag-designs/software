/**
 * @file gpio_utils.h
 * @brief Direct STM32 GPIO mode helpers for tag board lines.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_GPIO_UTILS_H
#define TAG_CORE_GPIO_UTILS_H

#include "hal.h"

/** @name GPIO mode helpers
 * Pin modification functions.
 *
 * The alternate function number is assumed to be set in board.h, so mode
 * configuration is a matter of setting the GPIO mode bits.
 * @{
 */

/**
 * @brief Put a board line in digital input mode.
 *
 * @param[in] line PAL line to reconfigure.
 */
static inline void toInput(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 0);
}

/**
 * @brief Put a board line in digital output mode.
 *
 * @param[in] line PAL line to reconfigure.
 */
static inline void toOutput(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 1 << (pin * 2));
}

/**
 * @brief Put a board line in alternate-function mode.
 *
 * @param[in] line PAL line to reconfigure.
 */
static inline void toAlternate(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 2 << (pin * 2));
}

/**
 * @brief Put a board line in analog mode for lowest leakage or ADC use.
 *
 * @param[in] line PAL line to reconfigure.
 */
static inline void toAnalog(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 3 << (pin * 2));
}
/** @} */

#endif
