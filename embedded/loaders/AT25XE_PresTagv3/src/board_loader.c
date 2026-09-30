/**
 * @file    board_loader.c
 * @brief   PresTagv3 board wiring for the AT25XE external loader.
 *
 * @details The AT25XE sits alone on SPI1 at PA4 (CS), PA5 (SCK), PA6 (MISO)
 *          and PA7 (MOSI), AF5 -- the same binding the firmware uses in
 *          families/PresTag/src/devices.c. It is powered from the main supply
 *          with no enable line, so it is reachable without power sequencing.
 *
 *          The LPS27 pressure sensor is on separate pins (PA10-12, PB3) behind
 *          its own switched rail (PB5). The loader leaves all of them as
 *          found: driving the sensor's pins with its rail off would
 *          back-power it.
 */

#include "loader.h"

/** @brief SPI1 alternate function for PA5-PA7 on STM32L432. */
#define PRESTAGV3_FLASH_SPI_AF 5U

/* Documented in loader.h. */
const LoaderSpiBus loaderFlashBus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .alternate_function = PRESTAGV3_FLASH_SPI_AF,
};

/* Contract documented in loader.h. */
bool loaderBoardInit(void)
{
  /* GPIOA carries all four flash lines. */
  rccEnableAHB2(RCC_AHB2ENR_GPIOAEN, false);
  return true;
}
