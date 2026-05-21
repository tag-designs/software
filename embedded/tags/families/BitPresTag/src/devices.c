#include "hal.h"

#include "custom.h"
#include "device.h"
#include "devices.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"
#include "timekeeping.h"

#if defined(TAG_FLASH_AT25XE)
#include "at25xe.h"
#define EXTERNAL_FLASH_OPS (&at25xeStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE AT25XE_SECTOR_SIZE
#define EXTERNAL_FLASH_SECTOR_COUNT AT25XE_SECTOR_COUNT
#elif defined(TAG_FLASH_MX25R)
#include "mx25r.h"
#define EXTERNAL_FLASH_OPS (&mx25rStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25R_SECTOR_SIZE
#define EXTERNAL_FLASH_SECTOR_COUNT MX25R_SECTOR_COUNT
#else
#error "BitPresTag family requires a supported external flash module"
#endif

#ifndef ACCEL_WAKEUP_SOURCE
#define ACCEL_WAKEUP_SOURCE 1
#endif

#if ACCEL_WAKEUP_SOURCE == 1
#define TAG_ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP1
#define TAG_ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP1_Msk
#elif ACCEL_WAKEUP_SOURCE == 4
#define TAG_ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP4
#define TAG_ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP4_Msk
#else
#error "Unsupported ACCEL_WAKEUP_SOURCE"
#endif

static void lpsSleepMilliseconds(int ms);

/*
 * Device descriptors.
 *
 * BitPresTag and BitPresTagMX25R share board wiring, pressure sensor, and
 * accelerometer. The only hardware difference in this family is external flash;
 * project.mk selects the flash module and the descriptor below binds that chip
 * operation table to the shared board wiring.
 */
static const TagSpiDevice external_flash_power = {
    TAG_SPI1_DEVICE_DEFAULTS,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .spi = &external_flash_power,
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXTERNAL_FLASH_SECTOR_COUNT,
};

static const TagUsartDevice lps_usart_device = {
    TAG_USART2_SYNC_DEVICE_DEFAULTS,
    .cs = LINE_LPS_CS,
    .sck = LINE_LPS_SCK,
    .tx = LINE_LPS_TX,
    .rx = LINE_LPS_RX,
    .pwr = LINE_LPS_PWR,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_FLOAT,
};

static const TagBusDevice lps_register_bus = {
    .ops = &tagUsartBusOps,
    .device = &lps_usart_device,
};

static const TagRegisterDevice lps_registers = {
    .read_register = tagStUsartReadRegisterDevice,
    .write_register = tagStUsartWriteRegisterDevice,
    .bus = &lps_register_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagBitPresTagPressureDevice = {
    .registers = &lps_registers,
    .sleep_ms = lpsSleepMilliseconds,
};

static const TagSpiDevice accel_bus = {
    TAG_SPI1_DEVICE_DEFAULTS,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .miso = LINE_ACCEL_MISO,
    .mosi = LINE_ACCEL_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagAdxl362Device tagBitPresTagAccelDevice = {
    .spi = &accel_bus,
};

/*
 * Helper bindings.
 *
 * The common pressure and accelerometer drivers are parameterized by device
 * descriptors. These small bindings connect generic code and self-tests to the
 * BitPresTag-family descriptors above.
 */
static void lpsSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

bool tag_test_external_flash(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool result = tagStorageCheckID(TAG_EXTERNAL_FLASH) > -1;
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  return result;
}

bool tag_test_lps27(void)
{
  return tagPressureTest();
}

static const TagTestCase tag_tests[] =
{
  {RUN_ADXL362, ADXL362_FAILED, tag_test_adxl362},
  {RUN_RTC, RTC_FAILED, tag_test_rtc},
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash},
  {RUN_LPS, LPS_FAILED, tag_test_lps27},
};

const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

const TagAdxl362Device *tagAdxl362Device(void)
{
  return TAG_ACCEL_DEVICE;
}

/*
 * Required standby hooks.
 *
 * pwr.c calls these hooks while entering standby. Device-specific storage logic
 * prepares external flash only in states where the log should be quiescent, then
 * this file applies the tag-family standby pin policy.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}

void tagDevicesDisableWakeupSources(void)
{
  CLEAR_BIT(PWR->CR3, TAG_ACCEL_WAKEUP_ENABLE_BIT);
}

bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active)
{
  if (is_active)
    SET_BIT(PWR->CR4, TAG_ACCEL_WAKEUP_POLARITY_BIT);
  else
    CLEAR_BIT(PWR->CR4, TAG_ACCEL_WAKEUP_POLARITY_BIT);

  if (state == RUNNING)
  {
    SET_BIT(PWR->CR3, TAG_ACCEL_WAKEUP_ENABLE_BIT | PWR_CR3_EIWF_Msk);
    return is_active == palReadLine(LINE_ACCEL_INT);
  }

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  return true;
}

void tagDevicesDeinit(void)
{
  ADXL362_DeinitDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(2);
}
