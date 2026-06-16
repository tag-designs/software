/**
 * @file devices.c
 * @brief BitTagNG family device descriptors, tests, and power hooks.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#include "hal.h"

#include "at25xe.h"
#include "core_sync.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"

binary_semaphore_t SPI1mutex;

void tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
}

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

const TagAdxl367Device tagBitTagNGAccelDevice = {
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_ACCEL_CS,
        .sck = LINE_ACCEL_SCK,
        .miso = LINE_ACCEL_MISO,
        .mosi = LINE_ACCEL_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
};

static const TagTestCase tag_tests[] =
{
  {RUN_ADXL362, tag_test_adxl367, TAG_ACCEL_DEVICE},
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
};

const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

const TagAdxl367Device *tagAdxl367Device(void)
{
  return TAG_ACCEL_DEVICE;
}

void tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state)
{
  switch (reason) {
  case TAG_DEVICE_POWER_STANDBY_ENTRY:
    tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
    break;

  case TAG_DEVICE_POWER_BOOT_CLEANUP:
  case TAG_DEVICE_POWER_RUNTIME_DEINIT:
  case TAG_DEVICE_POWER_TERMINAL_ENTRY:
  default:
    ADXL367_DeinitDevice(TAG_ACCEL_DEVICE);
    chThdSleepMilliseconds(2);
    break;
  }
}

void tagDevicesPrepareStandby(uint32_t state)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}

void tagDevicesDeinit(void)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_RUNTIME_DEINIT, 0);
}
