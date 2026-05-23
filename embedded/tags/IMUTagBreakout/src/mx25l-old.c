/**
 * @file mx25l-old.c
 * @brief Legacy polled-SPI MX25L flash driver retained for IMUTagBreakout.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "mx25l.h"
#include "hal.h"
#include "custom.h"
#include "app.h"
#include "math.h"
#include "stdint.h"
#include "external_flash.h"

// Flash command set
#define MX25_CMD_READ_ID            0x9FU
#define MX25_CMD_READ_DATA          0x03U
#define MX25_CMD_PAGE_PROGRAM       0x02U
#define MX25_CMD_WRITE_ENABLE       0x06U
#define MX25_CMD_READ_STATUS        0x05U
#define MX25_CMD_SECTOR_ERASE       0x20U
#define MX25_CMD_DEEP_POWER_DOWN    0xB9U
#define MX25_CMD_RELEASE_POWER_DOWN 0xABU
#define MX25_CMD_RESET_ENABLE       0x66U
#define MX25_CMD_RESET_MEMORY       0x99U

/* Status Register */

#define MX25_FLAGS_SR_WIP                    ((uint8_t)0x01)    /* Write in progress */
#define MX25_FLAGS_SR_WEL                    ((uint8_t)0x02)    /* Write enable latch */
#define MX25_FLAGS_SR_BP                     ((uint8_t)0x3C)    /* Block protect */
#define MX25_FLAGS_SR_QE                     ((uint8_t)0x40)    /* Quad enable */
#define MX25_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /* Status register write disable */


/*
 * SPI Hooks
*/

/**
 * @brief Transmit bytes using the legacy polled SPI1 path.
 *
 * @param[in] n Number of bytes to transmit.
 * @param[in] buf Bytes to transmit.
 */
static inline void SendPolled(uint32_t n, const uint8_t *buf)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  uint32_t rn = n;
  while (n || rn)
  {
    while (n && (SPI1->SR & SPI_SR_TXE)){
        *spidr = *buf++;
        n--;
    }
    while (rn && (SPI1->SR & SPI_SR_RXNE))
    {
        *spidr;
        rn--;
    }
  }
}

/**
 * @brief Receive bytes using the legacy polled SPI1 path.
 *
 * @param[in] n Number of bytes to receive.
 * @param[out] buf Destination buffer.
 */
static inline void ReceivePolled(uint32_t n, uint8_t *buf)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  uint32_t rn = n;
  while (n || rn)
  {
    while (n && (SPI1->SR & SPI_SR_TXE)){
        *spidr = 0xff;
        n--;
    }
    while (rn && (SPI1->SR & SPI_SR_RXNE))
    {
        *buf++ = *spidr;
        rn--;
    }
  }
}


// ============================================================
// Platform hooks
// ============================================================

#define spi_cs_low() palClearLine(LINE_MX_nCS)
#define spi_cs_high() palSetLine(LINE_MX_nCS)
#define spi_tx(txbuf, len) SendPolled(len,txbuf)
#define spi_rx(rxbuf, len) ReceivePolled(len,rxbuf)

#define delay_ms(ms) chThdSleepMilliseconds(ms)
#define delay_us(us) chThdSleepMicroseconds(us)


// ============================================================
// Internal defaults and state
// ============================================================
#define MX25_STATUS_WIP 0x01U

const mx25_config_t MX25L12845_DEFAULT_CONFIG = {
    .page_size = 256U,
    .sector_size = 4096U,
    .size_mb = 16U,
    .manufacturer_id = 0xC2U,
    .device_id = 0x20U,
    .capacity_id = 0x18U,
    .deep_power_down_us = 10U,
    .release_power_down_us = 30U,
    .wip_poll_us = 50U,
    .sector_erase_ms = 50U
};

static const mx25_config_t *g_cfg = &MX25L12845_DEFAULT_CONFIG;

// ============================================================
// Config access
// ============================================================
/**
 * @brief Select the active flash configuration.
 *
 * @param[in] cfg Configuration to use, or NULL to restore the default.
 */
void mx25_set_config(const mx25_config_t *cfg)
{
    g_cfg = (cfg != 0) ? cfg : &MX25L12845_DEFAULT_CONFIG;
}

/**
 * @brief Return the active flash configuration.
 *
 * @return Active MX25L configuration.
 */
const mx25_config_t *mx25_get_config(void)
{
    return g_cfg;
}

// ============================================================
// Helpers
// ============================================================
/**
 * @brief Set the flash write-enable latch.
 */
static void write_enable(void)
{
    uint8_t cmd = MX25_CMD_WRITE_ENABLE;
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_cs_high();
}

/**
 * @brief Read the flash status register.
 *
 * @return Status register value.
 */
static uint8_t read_status(void)
{
    uint8_t tx = MX25_CMD_READ_STATUS;
    uint8_t rx;

    spi_cs_low();
    spi_tx(&tx,1);
    spi_rx(&rx,1);
    spi_cs_high();

    return rx;
}

/**
 * @brief Poll until the flash write-in-progress bit clears.
 */
static void wait_until_ready(void)
{
    while ((read_status() & MX25_STATUS_WIP) != 0U)
    {
        delay_us(g_cfg->wip_poll_us);
    }
}

/**
 * @brief Program one page-aligned chunk.
 *
 * @param[in] address Flash address to program.
 * @param[in] data Bytes to write.
 * @param[in] length Number of bytes in this page chunk.
 */
static void page_program(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint8_t hdr[4];

    hdr[0] = MX25_CMD_PAGE_PROGRAM;
    hdr[1] = (uint8_t)((address >> 16) & 0xFFU);
    hdr[2] = (uint8_t)((address >> 8) & 0xFFU);
    hdr[3] = (uint8_t)(address & 0xFFU);

    write_enable();

    spi_cs_low();
    spi_tx(hdr, 4U);
    spi_tx(data, length);
    spi_cs_high();

    wait_until_ready();
}

// ============================================================
// Power control
// ============================================================
/**
 * @brief Put the MX25L into deep power-down mode.
 */
void mx25_deep_power_down(void)
{
    uint8_t cmd = MX25_CMD_DEEP_POWER_DOWN;
    FlashSpiOn();
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_cs_high();
    delay_us(g_cfg->deep_power_down_us);
    FlashSpiOff();
}

/**
 * @brief Release the MX25L from deep power-down mode.
 */
void mx25_release_power_down(void)
{
    uint8_t cmd = MX25_CMD_RELEASE_POWER_DOWN;
    uint8_t buf[3];

    FlashSpiOn();
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_rx(buf,3);
    spi_cs_high();
    delay_us(g_cfg->release_power_down_us);
    FlashSpiOff();
}

// ============================================================
// Identification
// ============================================================
/**
 * @brief Read and validate the MX25L identity bytes.
 *
 * @param[out] manufacturer Optional manufacturer ID.
 * @param[out] memory_type Optional memory type ID.
 * @param[out] capacity Optional capacity ID.
 * @param[out] size_mb Optional configured size in megabytes.
 * @return true when the identity matches the active configuration.
 */
bool mx25_read_id(uint8_t *manufacturer, uint8_t *memory_type, uint8_t *capacity, uint32_t *size_mb)
{
  uint8_t tx = MX25_CMD_READ_ID;
    uint8_t rx[3];

    FlashSpiOn();

    spi_cs_low();
    spi_tx(&tx,1);
    spi_rx(rx,3);
    spi_cs_high();

    FlashSpiOff();

    if (manufacturer != 0)
        *manufacturer = rx[1];
    if (memory_type != 0)
        *memory_type = rx[2];
    if (capacity != 0)
        *capacity = rx[3];

    if ((rx[0] != g_cfg->manufacturer_id) || (rx[2] != g_cfg->capacity_id)
        ||(rx[1] != g_cfg->device_id))
        return false;

    if (size_mb != 0)
        *size_mb = g_cfg->size_mb;

    return true;
}

// ============================================================
// Read / Write / Erase
// ============================================================
/**
 * @brief Read bytes from flash.
 *
 * @param[in] address Flash address to read.
 * @param[out] buffer Destination buffer.
 * @param[in] length Number of bytes to read.
 */
void mx25_read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    uint8_t hdr[4];

    FlashSpiOn();

    hdr[0] = MX25_CMD_READ_DATA;
    hdr[1] = (uint8_t)((address >> 16) & 0xFFU);
    hdr[2] = (uint8_t)((address >> 8) & 0xFFU);
    hdr[3] = (uint8_t)(address & 0xFFU);

    spi_cs_low();
    spi_tx(hdr, 4U);
    spi_rx(buffer, length);
    spi_cs_high();

    FlashSpiOff();
}

/**
 * @brief Program bytes to flash, splitting writes on page boundaries.
 *
 * @param[in] address Flash address to program.
 * @param[in] data Bytes to write.
 * @param[in] length Number of bytes to write.
 */
void mx25_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

    FlashSpiOn();

    while (length > 0U)
    {
        uint32_t page_offset = address % g_cfg->page_size;
        uint32_t chunk = g_cfg->page_size - page_offset;

        if (chunk > length)
            chunk = length;

        page_program(address, &data[offset], chunk);

        address += chunk;
        offset += chunk;
        length -= chunk;
    }

    FlashSpiOff();
}

/**
 * @brief Erase the sector containing an address.
 *
 * @param[in] address Address within the sector to erase.
 */
void mx25_erase_sector(uint32_t address)
{
    uint8_t cmd[4];

    cmd[0] = MX25_CMD_SECTOR_ERASE;
    cmd[1] = (uint8_t)((address >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((address >> 8) & 0xFFU);
    cmd[3] = (uint8_t)(address & 0xFFU);

    FlashSpiOn();

    write_enable();

    spi_cs_low();
    spi_tx(cmd, 4U);
    spi_cs_high();

    delay_ms(g_cfg->sector_erase_ms);
    wait_until_ready();

    FlashSpiOff();
}

/**
 * @brief Run the legacy MX25L identity self-test.
 *
 * @return true when the expected flash responds.
 */
bool mx25_test(){

    mx25_release_power_down();
    return mx25_read_id(0,0,0,0);
}
