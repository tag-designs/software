/**
 * @file mx25_chibios.c
 * @brief ChibiOS SPI adapter for the legacy MX25L flash driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "mx25l12845.h"

#define MX25_SPI_DRIVER SPID1

static const SPIConfig spicfg = {
    false,
    NULL,
    NULL,
    GPIOA,
    4,
    SPI_CR1_BR_1 | SPI_CR1_MSTR
};

/** @brief Assert chip select for the legacy MX25L driver. */
void spi_cs_low(void)
{
    spiSelect(&MX25_SPI_DRIVER);
}

/** @brief Deassert chip select for the legacy MX25L driver. */
void spi_cs_high(void)
{
    spiUnselect(&MX25_SPI_DRIVER);
}

/**
 * @brief Transmit bytes over the ChibiOS SPI driver.
 *
 * @param[in] txbuf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
void spi_tx(const uint8_t *txbuf, uint32_t len)
{
    spiSend(&MX25_SPI_DRIVER, len, txbuf);
}

/**
 * @brief Receive bytes over the ChibiOS SPI driver.
 *
 * @param[out] rxbuf Destination buffer.
 * @param[in] len Number of bytes to receive.
 */
void spi_rx(uint8_t *rxbuf, uint32_t len)
{
    spiReceive(&MX25_SPI_DRIVER, len, rxbuf);
}

/**
 * @brief Exchange bytes over the ChibiOS SPI driver.
 *
 * @param[in] txbuf Bytes to transmit.
 * @param[out] rxbuf Destination buffer.
 * @param[in] len Number of bytes to exchange.
 */
void spi_txrx(const uint8_t *txbuf, uint8_t *rxbuf, uint32_t len)
{
    spiExchange(&MX25_SPI_DRIVER, len, txbuf, rxbuf);
}

/**
 * @brief Delay for a number of milliseconds.
 *
 * @param[in] ms Delay in milliseconds.
 */
void delay_ms(uint32_t ms)
{
    chThdSleepMilliseconds(ms);
}

/**
 * @brief Delay for a number of microseconds.
 *
 * @param[in] us Delay in microseconds.
 */
void delay_us(uint32_t us)
{
    chThdSleepMicroseconds(us);
}

/**
 * @brief Start the ChibiOS SPI driver for the legacy MX25L adapter.
 */
void mx25_chibios_init(void)
{
    spiStart(&MX25_SPI_DRIVER, &spicfg);
}
