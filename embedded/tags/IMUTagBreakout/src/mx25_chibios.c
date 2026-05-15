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

void spi_cs_low(void)
{
    spiSelect(&MX25_SPI_DRIVER);
}

void spi_cs_high(void)
{
    spiUnselect(&MX25_SPI_DRIVER);
}

void spi_tx(const uint8_t *txbuf, uint32_t len)
{
    spiSend(&MX25_SPI_DRIVER, len, txbuf);
}

void spi_rx(uint8_t *rxbuf, uint32_t len)
{
    spiReceive(&MX25_SPI_DRIVER, len, rxbuf);
}

void spi_txrx(const uint8_t *txbuf, uint8_t *rxbuf, uint32_t len)
{
    spiExchange(&MX25_SPI_DRIVER, len, txbuf, rxbuf);
}

void delay_ms(uint32_t ms)
{
    chThdSleepMilliseconds(ms);
}

void delay_us(uint32_t us)
{
    chThdSleepMicroseconds(us);
}

void mx25_chibios_init(void)
{
    spiStart(&MX25_SPI_DRIVER, &spicfg);
}
