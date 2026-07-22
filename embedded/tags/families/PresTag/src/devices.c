/**
 * @file devices.c
 * @brief PresTag device descriptors, tests, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
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

/*
 * Device descriptors.
 *
 * PresTag owns the board-facing bindings for its non-universal devices here:
 * AT25XE external flash and the SPI LPS27 pressure sensor. Common drivers use
 * these descriptors instead of hard-coded board wiring.
 */
binary_semaphore_t SPI1mutex;

/**
 * @brief Initialize PresTag-owned bus synchronization state.
 */
void tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
}

static const TagRegisterDevice lps_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS(LINE_LPS_CS),
        //.cs = LINE_LPS_CS,
        .sck = LINE_LPS_SCK,
        .miso = LINE_LPS_MISO,
        .mosi = LINE_LPS_MOSI,
        .pwr = LINE_LPS_PWR,
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
        TAG_SPI1_DEVICE_DEFAULTS(LINE_FLASH_nCS),
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

static const TagTestCase tag_tests[] =
{
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_LPS, tag_test_lps27, TAG_PRESSURE_DEVICE},
};

/**
 * @brief Return the PresTag self-test table.
 *
 * @param[out] count Number of test cases.
 * @return Pointer to the static test-case table.
 */
const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

/*
 * Required standby hooks.
 *
 * pwr.c calls the protocol hook while entering standby. Generated boards apply
 * pin pulls through board_standby.h; this file keeps the pin hook as a legacy
 * fallback for static board builds.
 */
static void tagPresTagShutdownDevices(void)
{
  /*
   * The LPS27 path is one-shot and tagPressureDeviceEnd() powers down the
   * descriptor after each sample, so there is no additional sensor command here.
   */
}

static void tagPresTagPrepareStandbyDevices(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

/**
 * @brief Apply PresTag device power policy for a lifecycle phase.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
void tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state)
{
  switch (reason) {
  case TAG_DEVICE_POWER_STANDBY_ENTRY:
    tagPresTagPrepareStandbyDevices(state);
    break;

  case TAG_DEVICE_POWER_BOOT_CLEANUP:
  case TAG_DEVICE_POWER_RUNTIME_DEINIT:
  case TAG_DEVICE_POWER_TERMINAL_ENTRY:
  default:
    tagPresTagShutdownDevices();
    break;
  }
}

/**
 * @brief Prepare PresTag devices before entering standby.
 *
 * @param[in] state Current state-machine state.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, state);
}

/**
 * @brief Deinitialize PresTag-owned device resources.
 */
void tagDevicesDeinit(void)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_RUNTIME_DEINIT, 0);
}

/**
 * @brief Apply board pin pulls needed for standby leakage.
 */

 /*
void tagDevicesApplyStandbyPins(void)
{
  tagBusPrepareSleep(&lps_registers.bus);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}
*/
