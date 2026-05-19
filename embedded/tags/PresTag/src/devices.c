#include "hal.h"

#include "at25xe.h"
#include "custom.h"
#include "external_flash.h"
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

const TagStorageDevice tagExternalFlash = {
    .ops = &at25xeStorageOps,
    .spi = &external_flash_spi_bus,
    .enable = FlashSpiOn,
    .disable = FlashSpiOff,
    .sector_size = AT25XE_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / AT25XE_SECTOR_SIZE,
};
