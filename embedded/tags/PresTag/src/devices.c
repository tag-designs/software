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

static void lpsSleepMilliseconds(int ms);

/*
 * Device descriptors.
 *
 * PresTag owns the board-facing bindings for its non-universal devices here:
 * AT25XE external flash and the SPI LPS27 pressure sensor. Common drivers use
 * these descriptors instead of hard-coded board wiring.
 */
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

static const TagBusDevice lps_register_bus = {
    .ops = &tagSpiBusOps,
    .device = &lps_bus,
};

static const TagRegisterDevice lps_registers = {
    .read_register = tagStSpiReadRegisterDevice,
    .write_register = tagStSpiWriteRegisterDevice,
    .bus = &lps_register_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagPresTagPressureDevice = {
    .registers = &lps_registers,
    .sleep_ms = lpsSleepMilliseconds,
};

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

/*
 * Helper bindings.
 *
 * These small functions connect generic self-tests and pressure-driver timing
 * callbacks to the PresTag descriptors above.
 */
static void lpsSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

bool tag_test_lps27(void)
{
  return tagPressureTest();
}

bool tag_test_external_flash(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool result = tagStorageCheckID(TAG_EXTERNAL_FLASH) > -1;
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  return result;
}

static const TagTestCase tag_tests[] =
{
  {RUN_RTC, RTC_FAILED, tag_test_rtc},
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash},
  {RUN_LPS, LPS_FAILED, tag_test_lps27},
};

const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

/*
 * Required standby hooks.
 *
 * pwr.c calls these hooks while entering standby. PresTag prepares the pressure
 * sensor pins and applies the external-flash standby pin policy.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagSpiDevicePrepareSleep(&lps_bus);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}
