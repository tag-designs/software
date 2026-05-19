#include "power.h"

/*
 * Shared bus and standby-pin helpers.
 *
 * This file owns behavior that crosses bus types: line validation, MCU
 * standby pull registers, and Stop2 suspend/resume orchestration. SPI, I2C,
 * and USART-specific device/session mechanics live in their bus files.
 */

bool tagLineIsValid(ioline_t line)
{
  return line != TAG_NO_LINE;
}

void tagDisableActiveBusesForStop(void)
{
  tagSpiDisableActiveForStop();
  tagUsartDisableActiveForStop();
}

void tagEnableActiveBusesAfterStop(void)
{
  tagSpiEnableActiveAfterStop();
  tagUsartEnableActiveAfterStop();
}

void tagEnableStandbyPullup(ioline_t line)
{
  if (!tagLineIsValid(line))
  {
    return;
  }

  if ((PAL_PAD(line) != 14) && (PAL_PORT(line) == GPIOA))
  {
    SET_BIT(PWR->PUCRA, 1 << PAL_PAD(line));
  }
  if (PAL_PORT(line) == GPIOB)
  {
    SET_BIT(PWR->PUCRB, 1 << PAL_PAD(line));
  }
}

void tagEnableStandbyPulldown(ioline_t line)
{
  if (!tagLineIsValid(line))
  {
    return;
  }

  if ((PAL_PAD(line) != 13) && (PAL_PAD(line) != 15) && (PAL_PORT(line) == GPIOA))
  {
    SET_BIT(PWR->PDCRA, 1 << PAL_PAD(line));
  }
  if (PAL_PORT(line) == GPIOB)
  {
    SET_BIT(PWR->PDCRB, 1 << PAL_PAD(line));
  }
}
