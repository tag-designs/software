/**
 * @file bus_power.c
 * @brief Shared bus suspend/resume and STM32 standby pull helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "power.h"

/** @name Shared bus and standby-pin helpers
 * Shared bus and standby-pin helpers.
 *
 * This file owns behavior that crosses bus types: line validation, MCU
 * standby pull registers, and Stop2 suspend/resume orchestration. SPI, I2C,
 * and USART-specific device/session mechanics live in their bus files.
 * @{
 */

/**
 * @brief Test whether an optional board line is populated.
 *
 * @param[in] line PAL line value to validate.
 * @return true when line names a real board signal.
 */
bool tagLineIsValid(ioline_t line)
{
  return line != TAG_NO_LINE;
}

/**
 * @brief Suspend active SPI and USART buses before Stop2 entry.
 */
void tagDisableActiveBusesForStop(void)
{
  tagSpiDisableActiveForStop();
  tagUsartDisableActiveForStop();
}

/**
 * @brief Resume buses that were suspended for Stop2 entry.
 */
void tagEnableActiveBusesAfterStop(void)
{
  tagSpiEnableActiveAfterStop();
  tagUsartEnableActiveAfterStop();
}

/**
 * @brief Enable the STM32 standby pull-up for a valid board line.
 *
 * @param[in] line PAL line to bias during standby.
 */
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

/**
 * @brief Enable the STM32 standby pull-down for a valid board line.
 *
 * @param[in] line PAL line to bias during standby.
 */
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
/** @} */
