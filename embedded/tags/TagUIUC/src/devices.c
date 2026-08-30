/**
 * @file devices.c
 * @brief UIUC board device descriptors, tests, and power hooks.
 * @author tag firmware authors
 * @date 2026-08-30
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
#error "UIUC family requires a supported external flash module"
#endif

/*
 * Device descriptors.
 *
 * UIUC uses:
 * - ADXL367 on USART2 in 4-wire SPI mode (instead of ADXL362 on SPI2)
 * - BMP585 on SPI1 with LPS_RDY interrupt (instead of LPS27 on USART1)
 */
binary_semaphore_t SPI1mutex;
binary_semaphore_t USART2mutex;

/**
 * @brief Initialize UIUC-family bus synchronization state.
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
        .sck = LINE_FLASH_SCK,
        .miso = LINE_FLASH_MISO,
        .mosi = LINE_FLASH_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXTERNAL_FLASH_SECTOR_COUNT,
};

/* ADXL367 on USART2 synchronous clocked bus. */
const TagAdxl367Device tagUIUCAccelDevice = {
    .bus = TAG_BUS_USART_INIT(
        TAG_USART2_SYNC_DEVICE_DEFAULTS,
        .cs = LINE_ACCEL_nCS,
        .sck = LINE_ACCEL_SCK,
        .tx = LINE_ACCEL_MOSI,
        .rx = LINE_ACCEL_MISO,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_USART_SLEEP_SAFE_IDLE),
};

/* BMP585 on SPI1 with LPS_RDY interrupt */
static const TagRegisterDevice bmp585_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS(LINE_LPS_nCS),
        .sck = LINE_LPS_SCK,
        .miso = LINE_LPS_MISO,
        .mosi = LINE_LPS_MOSI,
        .pwr = LINE_LPS_PWR,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagUIUCPressureDevice = {
    .registers = &bmp585_registers,
};

static const TagTestCase tag_tests[] =
{
  {RUN_ADXL362, tag_test_adxl367, &tagUIUCAccelDevice},  // Uses ADXL367 test
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_LPS, tag_test_bmp581, &tagUIUCPressureDevice},   // Uses BMP581 test
};

/* Public API contract documented in test_support.h. */
const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

/**
 * @brief Return the UIUC ADXL367 descriptor.
 *
 * @return Accelerometer descriptor used by shared ADXL367 code.
 */
const TagAdxl367Device *tagAdxl367Device(void)
{
  return &tagUIUCAccelDevice;
}

/**
 * @brief Return the UIUC BMP585 descriptor.
 *
 * @return Pressure descriptor used by shared BMP581 code.
 */
const TagPressureDevice *tagPressureDevice(void)
{
  return &tagUIUCPressureDevice;
}

/*
 * Required standby hooks.
 */
static void tagUIDCShutdownDevices(void)
{
  tagBusPowerOff(&TAG_PRESSURE_DEVICE->registers->bus);
  ADXL367_DeinitDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(2);
}

/**
 * @brief Apply UIUC-family device power policy for a lifecycle phase.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
void tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state)
{
  switch (reason) {
  case TAG_DEVICE_POWER_STANDBY_ENTRY:
    tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
    if (state != RUNNING)
      tagUIDCShutdownDevices();
    break;

  case TAG_DEVICE_POWER_BOOT_CLEANUP:
  case TAG_DEVICE_POWER_RUNTIME_DEINIT:
  case TAG_DEVICE_POWER_TERMINAL_ENTRY:
  default:
    tagUIDCShutdownDevices();
    break;
  }
}

/**
 * @brief Prepare UIUC devices before entering standby.
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
  tagEnableStandbyPullup(LINE_ACCEL_nCS);
  tagEnableStandbyPullup(LINE_LPS_nCS);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}

/**
 * @brief Disable wakeup sources before reconfiguration.
 */
void tagDevicesDisableWakeupSources(void)
{
  CLEAR_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk | PWR_CR3_EIWF_Msk);
}

/**
 * @brief Configure wakeup sources.
 *
 * @param[in] state Current state-machine state.
 * @param[in] is_active Current accelerometer activity level.
 * @return true when the wake configuration matches the sampled line state.
 */
bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active)
{
  /* ADXL367 activity wake on WKUP1 */
  if (is_active)
    SET_BIT(PWR->CR4, PWR_CR4_WP1);
  else
    CLEAR_BIT(PWR->CR4, PWR_CR4_WP1);

  if (state == RUNNING) {
    SET_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk | PWR_CR3_EIWF_Msk);
    return is_active == palReadLine(LINE_WKUP1);
  }

  if ((state == CONFIGURED) || (state == HIBERNATING))
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
