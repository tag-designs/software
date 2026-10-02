/**
 * @file    board_loader.c
 * @brief   IMUTagNandv2 board wiring for the GD5F SPI-NAND loader.
 *
 * @details The GD5F2GM7RE is on SPI1 at PA4 (CS, named AT25_nCS on this
 *          board), PA5 (SCK), PA6 (MISO) and PA7 (MOSI), AF5 -- the binding
 *          the firmware uses (families/IMUTag/src/devices.c with
 *          IMUTagNandBmp581/inc/custom.h). It is powered from FLASH_PWR
 *          (PA8), which the firmware drives high from its board init (board.h:
 *          output, ODR high) and never changes. After the attach's reset PA8
 *          is undriven, so the loader drives it high before using the part.
 *          Without that, the part ran briefly on residual charge and then
 *          dropped off the bus (measured 2026-10-02; see gd5f_loader.c).
 *
 *          Powering the part, if it had lost power, starts it from power-on:
 *          its block-lock register reads 0x38 and the status of its last
 *          operation is gone. That is unavoidable, and the loader reports the
 *          registers as it reads them after power-up.
 *
 *          The LSM6DSV16X shares the NAND's SCK, MISO and MOSI nets and is
 *          always powered, so its chip select (PB1, LSM_CS) is driven high
 *          before any SPI traffic. Every other LSM6DSV pin, and the BMP581 on
 *          SPI1's other pin set, are left as found.
 */

#include "loader.h"

/** @brief Wait after powering the NAND: tVSL (2 ms) plus margin. */
#define IMUTAGNANDV2_FLASH_POWER_UP_MS 5U

/** @brief SPI1 alternate function for PA5-PA7 on STM32U375. */
#define IMUTAGNANDV2_FLASH_SPI_AF 5U

/* Documented in loader.h. */
const LoaderSpiBus loaderFlashBus = {
    .spi = SPI1,
    .cs = LINE_AT25_nCS,
    .sck = LINE_AT25_SCK,
    .miso = LINE_AT25_MISO,
    .mosi = LINE_AT25_MOSI,
    .alternate_function = IMUTAGNANDV2_FLASH_SPI_AF,
};

/* Contract documented in loader.h. */
bool loaderBoardInit(void)
{
  rccEnableAHB2R1(RCC_AHB2ENR1_GPIOAEN | RCC_AHB2ENR1_GPIOBEN, false);

  /* Keep the LSM6DSV16X off the shared bus. */
  palSetLine(LINE_LSM_CS);
  palSetLineMode(LINE_LSM_CS, PAL_MODE_OUTPUT_PUSHPULL);

  /*
   * Power the NAND. FLASH_PWR (PA8) supplies it on this board, and after the
   * attach's reset the pin is undriven. Allow tVSL (2 ms) plus the part's
   * power-on read of page 0 into its cache, with margin.
   */
  palSetLine(LINE_FLASH_PWR);
  palSetLineMode(LINE_FLASH_PWR, PAL_MODE_OUTPUT_PUSHPULL);
  loaderDelayMs(IMUTAGNANDV2_FLASH_POWER_UP_MS);
  return true;
}
