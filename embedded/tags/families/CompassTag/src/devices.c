#include "hal.h"

#include "ak09940a.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
#include "lis2du12.h"
#include "power.h"
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
#error "CompassTag family requires a supported external flash module"
#endif

#define COMPASS_ACCEL_WAKE_LINE LINE_WKUP1
#define TAG_ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP1
#define TAG_ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP1_Msk

/*
 * CompassTag-family device bindings.
 *
 * This file describes the concrete board wiring used by the CompassTag family:
 * AK09940A magnetometer on SPI1, LIS2DU12 accelerometer on synchronous USART2,
 * and external flash on SPI1. Common drivers consume these descriptors rather
 * than reaching through generic board globals.
 */

#if defined(LINE_MAG_PWR)
#define AK09940A_PWR LINE_MAG_PWR
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_FLOAT
#else
#define AK09940A_PWR TAG_NO_LINE
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_SAFE_IDLE
#endif

/*
 * Helper declarations.
 *
 * The descriptor table below is intentionally near the top of the file, so the
 * small tag-specific helper functions are declared here and defined below.
 */
static void compassTagMagSleepMilliseconds(int ms);
#if defined(LINE_MAG_TRG)
static void compassTagMagTriggerMode(bool output);
static void compassTagMagTrigger(void);
static bool compassTagMagDataReadyLine(void);
#endif
/*
 * Device descriptors.
 *
 * These structures are the single source of truth for family device wiring and
 * bus/register access. Keep board-line names here rather than spreading them
 * into drivers or application code.
 */
static const TagSpiDevice ak09940a_bus = {
    TAG_SPI1_DEVICE_DEFAULTS,
    .cs = LINE_MAG_CS,
    .sck = LINE_MAG_SCK,
    .miso = LINE_MAG_MISO,
    .mosi = LINE_MAG_MOSI,
    .pwr = AK09940A_PWR,
    .dummy = 0xff,
    .sleep_policy = AK09940A_SLEEP_POLICY,
};

static const TagBusDevice ak09940a_register_bus = {
    .ops = &tagSpiBusOps,
    .device = &ak09940a_bus,
};

static const TagRegisterDevice ak09940a_registers = {
    .read_register = tagStSpiReadRegisterDevice,
    .write_register = tagStSpiWriteRegisterDevice,
    .bus = &ak09940a_register_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagMagDevice tagCompassTagMagDevice = {
    .registers = &ak09940a_registers,
    .sleep_ms = compassTagMagSleepMilliseconds,
#if defined(LINE_MAG_TRG)
    .set_trigger_output = compassTagMagTriggerMode,
    .trigger = compassTagMagTrigger,
    .data_ready_line = compassTagMagDataReadyLine,
#else
    .set_trigger_output = 0,
    .trigger = 0,
    .data_ready_line = 0,
#endif
};

static const TagUsartDevice accel_usart_device = {
    TAG_USART2_SYNC_DEVICE_DEFAULTS,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .tx = LINE_ACCEL_TX,
    .rx = LINE_ACCEL_RX,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_SAFE_IDLE,
};

static const TagBusDevice accel_bus = {
    .ops = &tagUsartBusOps,
    .device = &accel_usart_device,
};

const TagRegisterDevice tagCompassTagAccelDevice = {
    .read_register = tagStUsartReadRegisterDevice,
    .write_register = tagStUsartWriteRegisterDevice,
    .bus = &accel_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

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

/*
 * Helper bindings.
 *
 * These functions adapt board-specific control lines and legacy self-test
 * entry points to the descriptors above.
 */
void tagCompassMagResetAssert(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palClearLine(LINE_MAG_RSTN);
#endif
}

void tagCompassMagResetRelease(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
#endif
}

static void compassTagMagSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

#if defined(LINE_MAG_TRG)
static void compassTagMagTriggerMode(bool output)
{
  if (output)
    toOutput(LINE_MAG_TRG);
  else
    toInput(LINE_MAG_TRG);
}

static void compassTagMagTrigger(void)
{
  palSetLine(LINE_MAG_TRG);
  palClearLine(LINE_MAG_TRG);
}

static bool compassTagMagDataReadyLine(void)
{
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
}
#endif

bool tag_test_ak09940a(void)
{
  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  if (TAG_MAG_DEVICE->sleep_ms)
    TAG_MAG_DEVICE->sleep_ms(1);
  tagCompassMagResetRelease();
  bool ok = ak09940aCheckWhoami(TAG_MAG_DEVICE);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);
  tagCompassMagResetAssert();
  return ok;
}

static const TagTestCase tag_tests[] =
{
  /*
   * The protocol predates AK09940A and still exposes RUN_MMC5633 for
   * magnetometer tests. Keep the request stable and report the specific
   * AK09940A failure result.
   */
  {RUN_MMC5633, AK09940A_FAILED, tag_test_ak09940a},

  /*
   * The protocol still uses RUN_AIS2 for newer low-power accelerometer tests.
   * Keep that mapping until the monitor protocol grows a generic RUN_ACCEL.
   */
  {RUN_AIS2, LIS2DU12_FAILED, tag_test_lis2du12},

  {RUN_RTC, RTC_FAILED, tag_test_rtc},
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash},
};

const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

const TagMagDevice *tagAk09940aDevice(void)
{
  return &tagCompassTagMagDevice;
}

const TagRegisterDevice *tagLis2du12Device(void)
{
  return &tagCompassTagAccelDevice;
}

bool tagCompassAccelWakeActive(void)
{
  return palReadLine(COMPASS_ACCEL_WAKE_LINE);
}

/*
 * Required standby hooks.
 *
 * The common power path calls these while entering low-power states. Device
 * preparation (storage sleep, reset assertion) is separate from MCU standby
 * pin-pull policy.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagCompassMagResetAssert();
  tagStoragePrepareStandby(&tagExternalFlash, state);
}

void tagDevicesApplyStandbyPins(void)
{
#if defined(LINE_MAG_RSTN)
  tagEnableStandbyPulldown(LINE_MAG_RSTN);
#endif
  tagSpiDevicePrepareSleep(&ak09940a_bus);
  tagUsartDevicePrepareSleep(&accel_usart_device);
  tagStorageApplyStandbyPins(&tagExternalFlash);
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
    return is_active == tagCompassAccelWakeActive();
  }

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  return true;
}

void tagDevicesDeinit(void)
{
  lis2du12Deinit(TAG_ACCEL_DEVICE);
  tagCompassMagResetAssert();
}
