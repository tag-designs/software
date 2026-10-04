/**
 * @file    loader.h
 * @brief   Shared services for STM32CubeProgrammer external loaders: clock,
 *          delay, polled SPI, and the board and part hooks a target supplies.
 *
 * @details A loader runs from SRAM, called by the programmer one entry point
 *          at a time, with interrupts disabled and no kernel. Everything here
 *          is therefore polled and bounded: no function waits on an
 *          interrupt, and every wait has a limit.
 *
 *          A target supplies two things: the board wiring of its external
 *          flash (::loaderFlashBus and loaderBoardInit()), and one part
 *          driver implementing loader_flash.h.
 *
 * @see     embedded/loaders/README.md, embedded/tags/design/proposals/field-data-extraction.md
 */

#ifndef LOADER_H
#define LOADER_H

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @def     LOADER_DEVICE_BASE
 * @brief   Address at which STM32CubeProgrammer sees the external flash.
 *
 * @details The flash is on plain SPI and is not memory mapped, so this
 *          address is fictional: the programmer passes addresses in
 *          [LOADER_DEVICE_BASE, LOADER_DEVICE_BASE + size) and the loader
 *          subtracts the base to get a flash offset. 0x90000000 is the
 *          conventional external-memory address used by ST's own loaders.
 */
#define LOADER_DEVICE_BASE 0x90000000UL

/**
 * @struct  LoaderSpiBus
 * @brief   Board wiring of the external flash's SPI bus.
 */
typedef struct {
  SPI_TypeDef *spi;            ///< SPI peripheral (SPI1 or SPI3 on STM32L432).
  ioline_t cs;                 ///< Chip select, driven as a GPIO, active low.
  ioline_t sck;                ///< Clock line, in alternate-function mode.
  ioline_t miso;               ///< Data from the flash, alternate function.
  ioline_t mosi;               ///< Data to the flash, alternate function.
  uint32_t alternate_function; ///< AF number for sck/miso/mosi.
} LoaderSpiBus;

/** @name Target-supplied board hooks
 * @{
 */
/** @brief SPI wiring of the external flash on this board. */
extern const LoaderSpiBus loaderFlashBus;

/**
 * @brief   Bring the board to a state in which the external flash is
 *          reachable.
 *
 * @details Called by every entry point after the clock is established and
 *          before the SPI bus is configured. Enables the GPIO clocks the
 *          flash lines need and performs any board-specific power sequencing.
 *          Must touch nothing beyond what reaching the flash requires: the
 *          loader may be run on a tag whose other state is still evidence.
 *
 * @return  true when the flash should now be reachable.
 */
bool loaderBoardInit(void);
/** @} */

/** @name Clock and delay (loader_clock.c, loader_delay.c)
 * @{
 */
/**
 * @brief   Switch SYSCLK to HSI16 without touching the backup domain.
 *
 * @details Enables HSI16, sets flash latency valid for 16 MHz in either
 *          core-voltage range, and selects HSI16 as SYSCLK with AHB and APB
 *          prescalers at 1. Idempotent. Writes only RCC_CR, RCC_CFGR and
 *          FLASH_ACR; never PWR or RCC_BDCR, so the RTC and backup registers
 *          are left exactly as the tag left them.
 *
 * @return  true when HSI16 is running and selected; false if either did not
 *          happen within a bounded poll.
 */
bool loaderClockInit(void);

/**
 * @brief   Start the DWT cycle counter used by loaderDelayUs().
 *
 * @pre     loaderClockInit() has succeeded, so SYSCLK is STM32_SYSCLK.
 */
void loaderDelayInit(void);

/**
 * @brief   Busy-wait for at least @p us microseconds.
 *
 * @param[in] us    Delay in microseconds. Values up to 60 s are exact to a
 *                  cycle; the counter wraps after about 268 s at 16 MHz.
 */
void loaderDelayUs(uint32_t us);

/**
 * @brief   Busy-wait for at least @p ms milliseconds.
 *
 * @param[in] ms    Delay in milliseconds.
 */
void loaderDelayMs(uint32_t ms);
/** @} */

/** @name Polled SPI (loader_spi.c)
 * Mode 0, 8-bit, SPI clock = PCLK / 2, matching the tag firmware's polled
 * configuration. Every transfer is bounded; a timeout returns false and
 * leaves chip select to the caller.
 * @{
 */
/**
 * @brief   Configure the flash's pins and SPI peripheral.
 *
 * @param[in] bus   Board wiring.
 *
 * @post    Chip select is driven high; the peripheral is enabled as master.
 */
void loaderSpiInit(const LoaderSpiBus *bus);

/** @brief Assert chip select (drive low). */
void loaderSpiSelect(const LoaderSpiBus *bus);

/** @brief Release chip select (drive high). */
void loaderSpiDeselect(const LoaderSpiBus *bus);

/**
 * @brief   Exchange one byte.
 *
 * @param[in]  bus  Board wiring.
 * @param[in]  tx   Byte to send.
 * @param[out] rx   Byte received; may be NULL.
 * @return  false if the peripheral did not complete the byte in time.
 */
bool loaderSpiExchange(const LoaderSpiBus *bus, uint8_t tx, uint8_t *rx);

/**
 * @brief   Send bytes, discarding what is received.
 *
 * @return  false on a transfer timeout.
 */
bool loaderSpiSend(const LoaderSpiBus *bus, const uint8_t *buf, uint32_t n);

/**
 * @brief   Receive bytes, sending 0xFF.
 *
 * @return  false on a transfer timeout.
 */
bool loaderSpiReceive(const LoaderSpiBus *bus, uint8_t *buf, uint32_t n);
/** @} */

#endif /* LOADER_H */
