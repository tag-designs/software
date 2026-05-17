#ifndef TAG_CORE_GPIO_UTILS_H
#define TAG_CORE_GPIO_UTILS_H

#include "hal.h"

//
// Pin modification functions.
//
// The alternate function number is assumed to be set in board.h, so mode
// configuration is a matter of setting the GPIO mode bits.
//

static inline void toInput(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 0);
}

static inline void toOutput(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 1 << (pin * 2));
}

static inline void toAlternate(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 2 << (pin * 2));
}

static inline void toAnalog(ioline_t line)
{
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 3 << (pin * 2));
}

#endif
