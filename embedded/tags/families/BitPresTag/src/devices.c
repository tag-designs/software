#include "hal.h"

#include "custom.h"
#include "device.h"
#include "power.h"
#include "storage_device.h"
#include "storage_flash.h"

#if defined(TAG_FLASH_AT25XE)
#include "at25xe.h"
#define EXTERNAL_FLASH_OPS (&at25xeStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE AT25XE_SECTOR_SIZE
#elif defined(TAG_FLASH_MX25R)
#include "mx25r.h"
#define EXTERNAL_FLASH_OPS (&mx25rStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25R_SECTOR_SIZE
#else
#error "BitPresTag family requires a supported external flash module"
#endif

/*
 * BitPresTag-family board bindings for shared external-flash storage code.
 *
 * BitPresTag and BitPresTagMX25R share board wiring and differ only in the
 * flash module selected by project.mk. The selected module chooses the chip
 * operation table above; this family descriptor supplies the common board
 * wiring, enable hooks, and geometry.
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
    .ops = EXTERNAL_FLASH_OPS,
    .spi = &external_flash_spi_bus,
    .power = &external_flash_power,
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / EXTERNAL_FLASH_SECTOR_SIZE,
};

void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(&tagExternalFlash, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagStorageApplyStandbyPins(&tagExternalFlash);
}
