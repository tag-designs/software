#ifndef MX25L_H
#define MX25L_H

#include <stdint.h>
#include <stdbool.h>



typedef struct
{
    uint32_t page_size;
    uint32_t sector_size;
    uint32_t size_mb;

    uint8_t manufacturer_id;
    uint8_t device_id;
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

bool mx25_test();

#endif
