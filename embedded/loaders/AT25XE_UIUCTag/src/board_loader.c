/**
 * @file    board_loader.c
 * @brief   UIUCTag board wiring for the AT25XE external loader.
 *
 * @details The AT25XE is on SPI1 at PA15 (CS), PB3 (SCK), PB4 (MISO) and PB5
 *          (MOSI), AF5 -- the binding the firmware uses in
 *          UIUCTag/src/devices.c, and the same pins as CompassTagv1. It is
 *          powered from the main supply with no enable line.
 *
 *          The BMP585 pressure sensor shares SPI1 in the firmware, but on
 *          other pins (SCK PA5, MISO PA11, MOSI PA12, CS PB0) behind its own
 *          switched rail (LPS_PWR, PB1). The loader routes SPI1 to PB3-PB5
 *          only and leaves every sensor pin as found: after reset the rail is
 *          off, and driving the sensor's pins would back-power it. The ADXL367
 *          accelerometer is on USART2 and is not touched either.
 *
 *          PA15, PB3 and PB4 come out of reset as JTAG pins. Reassigning them
 *          does not disturb SWD, which uses only PA13 and PA14.
 */

#include "loader.h"

/** @brief SPI1 alternate function for PB3-PB5 on STM32L432. */
#define UIUCTAG_FLASH_SPI_AF 5U

/* Documented in loader.h. */
const LoaderSpiBus loaderFlashBus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .alternate_function = UIUCTAG_FLASH_SPI_AF,
};

/* Contract documented in loader.h. */
bool loaderBoardInit(void)
{
  /* CS is on GPIOA, the three SPI lines on GPIOB. */
  rccEnableAHB2(RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN, false);
  return true;
}
