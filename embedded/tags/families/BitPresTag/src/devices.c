/**
 * @file devices.c
 * @brief BitPresTag family device descriptors, tests, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "core_sync.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"

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

/*
 * Device descriptors.
 *
 * BitPresTag and BitPresTagMX25R share board wiring, pressure sensor, and
 * accelerometer. The only hardware difference in this family is external flash;
 * project.mk selects the flash module and the descriptor below binds that chip
 * operation table to the shared board wiring.
 */
binary_semaphore_t SPI1mutex;
binary_semaphore_t USART2mutex;

/**
 * @brief Initialize BitPresTag-family bus synchronization state.
 */
void tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
  chBSemObjectInit(&USART2mutex, false);
}

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS(LINE_FLASH_nCS),
        //.cs = LINE_FLASH_nCS,
        .sck = LINE_FLASH_SCK,
        .miso = LINE_FLASH_MISO,
        .mosi = LINE_FLASH_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXTERNAL_FLASH_SECTOR_COUNT,
};

static const TagRegisterDevice lps_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_USART_INIT(
        TAG_USART2_SYNC_DEVICE_DEFAULTS,
        //.cs = LINE_LPS_CS,
        .sck = LINE_LPS_SCK,
        .tx = LINE_LPS_TX,
        .rx = LINE_LPS_RX,
        .pwr = LINE_LPS_PWR,
        .dummy = 0xff,
        .sleep_policy = TAG_USART_SLEEP_FLOAT),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagBitPresTagPressureDevice = {
    .registers = &lps_registers,
};

const TagAdxl362Device tagBitPresTagAccelDevice = {
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS(LINE_ACCEL_CS),
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
  {RUN_ADXL362, tag_test_adxl362, TAG_ACCEL_DEVICE},
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_LPS, tag_test_lps27, TAG_PRESSURE_DEVICE},
};

/**
 * @brief Return the BitPresTag family self-test table.
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
 * @brief Return the BitPresTag family ADXL362 descriptor.
 *
 * @return Accelerometer descriptor used by shared ADXL362 code.
 */
const TagAdxl362Device *tagAdxl362Device(void)
{
  return TAG_ACCEL_DEVICE;
}

/*
 * Required standby hooks.
 *
 * pwr.c calls the protocol hook while entering standby. Generated boards apply
 * pin pulls through board_standby.h; this file keeps the pin hook as a legacy
 * fallback for static board builds.
 */
static void tagBitPresTagShutdownDevices(void)
{
  ADXL362_DeinitDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(2);
}

/**
 * @brief Apply BitPresTag-family device power policy for a lifecycle phase.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
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
    tagBitPresTagShutdownDevices();
    break;
  }
}

/**
 * @brief Prepare BitPresTag devices before entering standby.
 *
 * @param[in] state Current state-machine state.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, state);
}

/**
 * @brief Apply board pin pulls needed for standby leakage and wake behavior.
 */
void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
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
    return is_active == palReadLine(LINE_WKUP1);
  }

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  return true;
}

/**
 * @brief Reset family sensors before returning to idle or shutdown states.
 */
void tagDevicesDeinit(void)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_RUNTIME_DEINIT, 0);
}
