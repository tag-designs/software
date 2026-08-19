/**
 * @file mx25l.h
 * @brief Legacy IMUTagBreakout MX25L flash driver API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef MX25L_H
#define MX25L_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Timing and identity configuration for a supported MX25L flash. */
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

/** Default configuration for MX25L12845. */
extern const mx25_config_t MX25L12845_DEFAULT_CONFIG;

/** @brief Select the MX25L configuration used by the legacy driver. */
void mx25_set_config(const mx25_config_t *cfg);
/** @brief Return the active MX25L configuration. */
const mx25_config_t *mx25_get_config(void);

/** @brief Put the flash into deep power-down mode. */
void mx25_deep_power_down(void);
/** @brief Release the flash from deep power-down mode. */
void mx25_release_power_down(void);

/** @brief Read and validate the flash identity bytes. */
bool mx25_read_id(uint8_t *manufacturer, uint8_t *memory_type, uint8_t *capacity, uint32_t *size_mb);

/** @brief Read bytes from flash. */
void mx25_read(uint32_t address, uint8_t *buffer, uint32_t length);
/** @brief Program bytes to flash. */
void mx25_write(uint32_t address, const uint8_t *data, uint32_t length);

/** @brief Erase a flash sector containing the address. */
void mx25_erase_sector(uint32_t address);

/** @brief Run the flash identity self-test. */
bool mx25_test();

#endif
