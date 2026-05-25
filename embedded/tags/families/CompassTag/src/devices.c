/**
 * @file devices.c
 * @brief CompassTag family device descriptors, tests, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "ak09940a.h"
#include "core_sync.h"
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
binary_semaphore_t SPI1mutex;
binary_semaphore_t USART2mutex;

/**
 * @brief Initialize CompassTag-family bus synchronization state.
 */
void tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
  chBSemObjectInit(&USART2mutex, false);
}

#if defined(LINE_MAG_PWR)
#define AK09940A_PWR LINE_MAG_PWR
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_FLOAT
#else
#define AK09940A_PWR TAG_NO_LINE
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_SAFE_IDLE
#endif

/*
 * Device descriptors.
 *
 * These structures are the single source of truth for family device wiring and
 * bus/register access. Keep board-line names here rather than spreading them
 * into drivers or application code.
 */
const TagRegisterDevice tagCompassTagMagDevice = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_MAG_CS,
        .sck = LINE_MAG_SCK,
        .miso = LINE_MAG_MISO,
        .mosi = LINE_MAG_MOSI,
        .pwr = AK09940A_PWR,
        .dummy = 0xff,
        .sleep_policy = AK09940A_SLEEP_POLICY),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagRegisterDevice tagCompassTagAccelDevice = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_USART_INIT(
        TAG_USART2_SYNC_DEVICE_DEFAULTS,
        .cs = LINE_ACCEL_CS,
        .sck = LINE_ACCEL_SCK,
        .tx = LINE_ACCEL_TX,
        .rx = LINE_ACCEL_RX,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_USART_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_FLASH_nCS,
        .sck = LINE_FLASH_SCK,
        .miso = LINE_FLASH_MISO,
        .mosi = LINE_FLASH_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXTERNAL_FLASH_SECTOR_COUNT,
};

/*
 * Helper bindings.
 *
 * These functions adapt board-specific control lines and legacy self-test
 * entry points to the descriptors above.
 */
/**
 * @brief Assert the AK09940A reset line when the board provides one.
 */
void tagCompassMagResetAssert(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palClearLine(LINE_MAG_RSTN);
#endif
}

/**
 * @brief Release the AK09940A reset line when the board provides one.
 */
void tagCompassMagResetRelease(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
#endif
}

/**
 * @brief Run the CompassTag AK09940A presence test.
 *
 * @param[in] context Optional TagRegisterDevice descriptor.
 * @return true when the magnetometer identity registers are valid.
 */
bool tag_test_ak09940a(const void *context)
{
  const TagRegisterDevice *device = context ? context : TAG_MAG_DEVICE;

  ak09940aDeviceBegin(device);
  stopMilliseconds(1);
  tagCompassMagResetRelease();
  bool ok = ak09940aCheckWhoami(device);
  ak09940aDeviceEnd(device);
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
  {RUN_MMC5633, AK09940A_FAILED, tag_test_ak09940a, TAG_MAG_DEVICE},

  /*
   * The protocol still uses RUN_AIS2 for newer low-power accelerometer tests.
   * Keep that mapping until the monitor protocol grows a generic RUN_ACCEL.
   */
  {RUN_AIS2, LIS2DU12_FAILED, tag_test_lis2du12, TAG_ACCEL_DEVICE},

  {RUN_RTC, RTC_FAILED, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash,
   TAG_EXTERNAL_FLASH},
};

/**
 * @brief Return the CompassTag family self-test table.
 *
 * @param[out] count Number of test cases.
 * @return Pointer to the static test-case table.
 */
const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

/**
 * @brief Return the CompassTag AK09940A descriptor.
 *
 * @return Magnetometer register descriptor used by shared AK09940A code.
 */
const TagRegisterDevice *tagAk09940aDevice(void)
{
  return &tagCompassTagMagDevice;
}

/**
 * @brief Return the CompassTag LIS2DU12 descriptor.
 *
 * @return Accelerometer register descriptor used by family code.
 */
const TagRegisterDevice *tagLis2du12Device(void)
{
  return &tagCompassTagAccelDevice;
}

/**
 * @brief Read the accelerometer wake line.
 *
 * @return true when the line is active high.
 */
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
/**
 * @brief Prepare CompassTag devices before entering standby.
 *
 * @param[in] state Current state-machine state.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagCompassMagResetAssert();
  tagStoragePrepareStandby(&tagExternalFlash, state);
}

/**
 * @brief Apply board pin pulls needed for standby leakage and wake behavior.
 */
void tagDevicesApplyStandbyPins(void)
{
#if defined(LINE_MAG_RSTN)
  tagEnableStandbyPulldown(LINE_MAG_RSTN);
#endif
  tagBusPrepareSleep(&tagCompassTagMagDevice.bus);
  tagBusPrepareSleep(&tagCompassTagAccelDevice.bus);
  tagStorageApplyStandbyPins(&tagExternalFlash);
}

/**
 * @brief Disable accelerometer wakeup sources before reconfiguration.
 */
void tagDevicesDisableWakeupSources(void)
{
  CLEAR_BIT(PWR->CR3, TAG_ACCEL_WAKEUP_ENABLE_BIT);
}

/**
 * @brief Configure accelerometer wakeup polarity and enable bits.
 *
 * @param[in] state Current state-machine state.
 * @param[in] is_active Current accelerometer activity level.
 * @return true when the wake configuration matches the sampled line state.
 */
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

/**
 * @brief Reset family sensors before returning to idle or shutdown states.
 */
void tagDevicesDeinit(void)
{
  lis2du12Deinit(TAG_ACCEL_DEVICE);
  tagCompassMagResetAssert();
}
