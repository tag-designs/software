#include "mx25l.h"
#include "hal.h"
#include "custom.h"
#include "app.h"
#include "math.h"
#include "stdint.h"

// Flash command set
#define MX25_CMD_READ_ID            0x9FU
#define MX25_CMD_READ_DATA          0x03U
#define MX25_CMD_PAGE_PROGRAM       0x02U
#define MX25_CMD_WRITE_ENABLE       0x06U
#define MX25_CMD_READ_STATUS        0x05U
#define MX25_CMD_SECTOR_ERASE       0x20U
#define MX25_CMD_DEEP_POWER_DOWN    0xB9U
#define MX25_CMD_RELEASE_POWER_DOWN 0xABU

/*
 * SPI Hooks
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
void mx25_set_config(const mx25_config_t *cfg)
{
    g_cfg = (cfg != 0) ? cfg : &MX25L12845_DEFAULT_CONFIG;
}

const mx25_config_t *mx25_get_config(void)
{
    return g_cfg;
}

// ============================================================
// Helpers
// ============================================================
static void write_enable(void)
{
    uint8_t cmd = MX25_CMD_WRITE_ENABLE;
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_cs_high();
}

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

static void wait_until_ready(void)
{
    while ((read_status() & MX25_STATUS_WIP) != 0U)
    {
        delay_us(g_cfg->wip_poll_us);
    }
}

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

bool mx25_test(){

    mx25_release_power_down();
    return mx25_read_id(0,0,0,0);
}
