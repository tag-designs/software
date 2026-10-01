/**
 * @file    board_loader.c
 * @brief   CompassTagv1 board wiring for the AT25XE external loader.
 *
 * @details The AT25XE is on SPI1 at PA15 (CS), PB3 (SCK), PB4 (MISO) and PB5
 *          (MOSI), AF5 -- the binding the firmware uses in
 *          families/CompassTag/src/devices.c. It is powered from the main
 *          supply with no enable line, so it is reachable without power
 *          sequencing.
 *
 *          The AK09940A magnetometer shares SPI1 in the firmware, but on the
 *          other SPI1 pin set (PA5-PA7, CS PB1) behind its own switched rail
 *          (MAG_PWR, PA9). The loader routes SPI1 to PB3-PB5 only and leaves
 *          every magnetometer pin as found: after reset its rail is off, and
 *          driving its pins would back-power it. The LIS2DU12 accelerometer is
 *          on USART2 and is not touched either.
 *
 *          PA15, PB3 and PB4 come out of reset as JTAG pins (JTDI, JTDO,
 *          NJTRST). Reassigning them does not disturb SWD, which uses only
 *          PA13 and PA14.
 */

#include "loader.h"

/** @brief SPI1 alternate function for PB3-PB5 on STM32L432. */
#define COMPASSTAGV1_FLASH_SPI_AF 5U

/* Documented in loader.h. */
const LoaderSpiBus loaderFlashBus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .alternate_function = COMPASSTAGV1_FLASH_SPI_AF,
};

/* Contract documented in loader.h. */
bool loaderBoardInit(void)
{
  /* CS is on GPIOA, the three SPI lines on GPIOB. */
  rccEnableAHB2(RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN, false);
  return true;
}
