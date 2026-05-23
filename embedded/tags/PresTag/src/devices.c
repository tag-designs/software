#include "hal.h"

#include "at25xe.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"

/*
 * Device descriptors.
 *
 * PresTag owns the board-facing bindings for its non-universal devices here:
 * AT25XE external flash and the SPI LPS27 pressure sensor. Common drivers use
 * these descriptors instead of hard-coded board wiring.
 */
static const TagRegisterDevice lps_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_STEVAL_CS,
        .sck = LINE_STEVAL_SCK,
        .miso = LINE_STEVAL_MISO,
        .mosi = LINE_STEVAL_MOSI,
        .pwr = LINE_STEVAL_PWR,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_FLOAT),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagPresTagPressureDevice = {
    .registers = &lps_registers,
};

const TagStorageDevice tagExternalFlash = {
    .ops = &at25xeStorageOps,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_FLASH_nCS,
        .sck = LINE_FLASH_SCK,
        .miso = LINE_FLASH_MISO,
        .mosi = LINE_FLASH_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = AT25XE_SECTOR_SIZE,
    .sector_count = AT25XE_SECTOR_COUNT,
};

/*
 * Helper bindings.
 *
 * These small functions connect generic self-tests to the PresTag descriptors
 * above.
 */
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
  tagBusPrepareSleep(&lps_registers.bus);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}
