#include "mx25l12845.h"

// ============================================================
// Platform hooks
// ============================================================
extern void spi_cs_low(void);
extern void spi_cs_high(void);

extern void spi_tx(const uint8_t *txbuf, uint32_t len);
extern void spi_rx(uint8_t *rxbuf, uint32_t len);
extern void spi_txrx(const uint8_t *txbuf, uint8_t *rxbuf, uint32_t len);

extern void delay_ms(uint32_t ms);
extern void delay_us(uint32_t us);

// ============================================================
// Internal defaults and state
// ============================================================
#define MX25_STATUS_WIP 0x01U

const mx25_config_t MX25L12845_DEFAULT_CONFIG = {
    .page_size = 256U,
    .sector_size = 4096U,
    .size_mb = 16U,
    .manufacturer_id = 0xC2U,
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
    uint8_t tx[2] = {MX25_CMD_READ_STATUS, 0xFFU};
    uint8_t rx[2];

    spi_cs_low();
    spi_txrx(tx, rx, 2U);
    spi_cs_high();

    return rx[1];
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
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_cs_high();
    delay_us(g_cfg->deep_power_down_us);
}

void mx25_release_power_down(void)
{
    uint8_t cmd = MX25_CMD_RELEASE_POWER_DOWN;
    spi_cs_low();
    spi_tx(&cmd, 1U);
    spi_cs_high();
    delay_us(g_cfg->release_power_down_us);
}

// ============================================================
// Identification
// ============================================================
bool mx25_read_id(uint8_t *manufacturer, uint8_t *memory_type, uint8_t *capacity, uint32_t *size_mb)
{
    uint8_t tx[4] = {MX25_CMD_READ_ID, 0xFFU, 0xFFU, 0xFFU};
    uint8_t rx[4];

    spi_cs_low();
    spi_txrx(tx, rx, 4U);
    spi_cs_high();

    if (manufacturer != 0)
        *manufacturer = rx[1];
    if (memory_type != 0)
        *memory_type = rx[2];
    if (capacity != 0)
        *capacity = rx[3];

    if ((rx[1] != g_cfg->manufacturer_id) || (rx[3] != g_cfg->capacity_id))
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

    hdr[0] = MX25_CMD_READ_DATA;
    hdr[1] = (uint8_t)((address >> 16) & 0xFFU);
    hdr[2] = (uint8_t)((address >> 8) & 0xFFU);
    hdr[3] = (uint8_t)(address & 0xFFU);

    spi_cs_low();
    spi_tx(hdr, 4U);
    spi_rx(buffer, length);
    spi_cs_high();
}

void mx25_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

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
}

void mx25_erase_sector(uint32_t address)
{
    uint8_t cmd[4];

    cmd[0] = MX25_CMD_SECTOR_ERASE;
    cmd[1] = (uint8_t)((address >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((address >> 8) & 0xFFU);
    cmd[3] = (uint8_t)(address & 0xFFU);

    write_enable();

    spi_cs_low();
    spi_tx(cmd, 4U);
    spi_cs_high();

    delay_ms(g_cfg->sector_erase_ms);
    wait_until_ready();
}
