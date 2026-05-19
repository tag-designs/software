#include "hal.h"

#include "at25xe.h"
#include "custom.h"
#include "power.h"
#include "storage_device.h"

/*
 * PresTag board bindings for shared external-flash storage code.
 *
 * The AT25XE driver owns the chip command set; this file owns the board-facing
 * descriptor: SPI controller, chip select line, board-level bus enable hooks,
 * and flash geometry.
 */
static const TagSpiBus external_flash_spi_bus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .dummy = 0xff,
};

static const TagSpiDevice external_flash_power = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = &at25xeStorageOps,
    .spi = &external_flash_spi_bus,
    .power = &external_flash_power,
    .sector_size = AT25XE_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / AT25XE_SECTOR_SIZE,
};
