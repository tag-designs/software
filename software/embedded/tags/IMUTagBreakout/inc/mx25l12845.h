#ifndef MX25L12845_H
#define MX25L12845_H

#include <stdint.h>
#include <stdbool.h>

// Flash command set
#define MX25_CMD_READ_ID            0x9FU
#define MX25_CMD_READ_DATA          0x03U
#define MX25_CMD_PAGE_PROGRAM       0x02U
#define MX25_CMD_WRITE_ENABLE       0x06U
#define MX25_CMD_READ_STATUS        0x05U
#define MX25_CMD_SECTOR_ERASE       0x20U
#define MX25_CMD_DEEP_POWER_DOWN    0xB9U
#define MX25_CMD_RELEASE_POWER_DOWN 0xABU

typedef struct
{
    uint32_t page_size;
    uint32_t sector_size;
    uint32_t size_mb;

    uint8_t manufacturer_id;
    uint8_t capacity_id;

    uint32_t deep_power_down_us;
    uint32_t release_power_down_us;
    uint32_t wip_poll_us;
    uint32_t sector_erase_ms;
} mx25_config_t;

// Default configuration for MX25L12845
extern const mx25_config_t MX25L12845_DEFAULT_CONFIG;

void mx25_set_config(const mx25_config_t *cfg);
const mx25_config_t *mx25_get_config(void);

void mx25_deep_power_down(void);
void mx25_release_power_down(void);

bool mx25_read_id(uint8_t *manufacturer, uint8_t *memory_type, uint8_t *capacity, uint32_t *size_mb);

void mx25_read(uint32_t address, uint8_t *buffer, uint32_t length);
void mx25_write(uint32_t address, const uint8_t *data, uint32_t length);

void mx25_erase_sector(uint32_t address);

#endif
