#include "hal.h"

#include "at25xe.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "lps.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"
#include "timekeeping.h"

/*
 * PresTag board bindings for non-universal devices.
 *
 * pwr.c owns the universal RTC and standby sequence. This file owns the
 * board-facing descriptors and legacy wrapper functions for peripherals that
 * are specific to this tag: external flash and the SPI pressure sensor.
 */

#ifdef LPS_SPI
static const TagSpiDevice lps_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_STEVAL_CS,
    .sck = LINE_STEVAL_SCK,
    .miso = LINE_STEVAL_MISO,
    .mosi = LINE_STEVAL_MOSI,
    .pwr = LINE_STEVAL_PWR,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};

static const TagStSpiRegisterBus lps_register_spi = {
    .device = &lps_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus lps_registers = {
    .read_register = tagStSpiReadRegister,
    .write_register = tagStSpiWriteRegister,
    .context = &lps_register_spi,
};

static void lpsSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

const TagPressureDevice tagPresTagPressureDevice = {
    .registers = &lps_registers,
    .sleep_ms = lpsSleepMilliseconds,
};

bool tag_test_lps27(void)
{
  return tagPressureTest();
}
#endif

static const TagSpiDevice external_flash_power = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = &at25xeStorageOps,
    .spi = &external_flash_power,
    .sector_size = AT25XE_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / AT25XE_SECTOR_SIZE,
};

bool tag_test_external_flash(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool result = tagStorageCheckID(TAG_EXTERNAL_FLASH) > -1;
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  return result;
}

void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(&tagExternalFlash, state);
}

void tagDevicesApplyStandbyPins(void)
{
#ifdef LPS_SPI
  tagSpiDevicePrepareSleep(&lps_bus);
#endif
  tagStorageApplyStandbyPins(&tagExternalFlash);
}
