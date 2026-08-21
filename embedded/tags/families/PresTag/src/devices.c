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
        //.cs = LINE_FLASH_nCS,
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

/* Public API contract documented in test_support.h. */
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
/**
 * @brief Release PresTag pressure-sensor signal pins after sensor power-off.
 *
 * @details The LPS rail is switched off in inactive sleep states, so the SPI
 *          signal pins must not be driven above the unpowered sensor rail.
 *          Leaving the communication pins in analog mode removes active GPIO
 *          drive; standby pull policy can still bias selected nets low during
 *          the low-power interval.
 */
static void tagPresTagReleasePressurePins(void)
{
  palSetLineMode(LINE_LPS_CS, PAL_MODE_INPUT_ANALOG);
  palSetLineMode(LINE_LPS_SCK, PAL_MODE_INPUT_ANALOG);
  palSetLineMode(LINE_LPS_MISO, PAL_MODE_INPUT_ANALOG);
  palSetLineMode(LINE_LPS_MOSI, PAL_MODE_INPUT_ANALOG);
}

void tagPressureDeviceAfterPowerOff(const TagPressureDevice *device)
{
  if (device == TAG_PRESSURE_DEVICE)
    tagPresTagReleasePressurePins();
}

static void tagPresTagShutdownDevices(void)
{
  tagBusPowerOff(&TAG_PRESSURE_DEVICE->registers->bus);
  tagPresTagReleasePressurePins();
  palClearLine(LINE_LPS_PWR);
  palSetLineMode(LINE_LPS_PWR,
                 PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_LOWEST);
  chThdSleepMilliseconds(2);
}

static void tagPresTagPrepareStandbyDevices(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

static void tagPresTagReleaseDebugPins(void)
{
  palSetLineMode(LINE_SWDIO, PAL_MODE_INPUT_ANALOG);
  palSetLineMode(LINE_SWCLK, PAL_MODE_INPUT_ANALOG);
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
    tagPresTagReleaseDebugPins();
    if (state != RUNNING)
      tagPresTagShutdownDevices();
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
void tagDevicesApplyStandbyPins(void)
{
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
  tagEnableStandbyPulldown(LINE_FLASH_MISO);
  tagEnableStandbyPulldown(LINE_LPS_SCK);
  tagEnableStandbyPulldown(LINE_LPS_MISO);
  tagEnableStandbyPulldown(LINE_LPS_MOSI);
  tagEnableStandbyPulldown(LINE_LPS_PWR);
}

/**
 * @brief Disable PresTag wakeup sources before terminal sleep setup.
 */
void tagDevicesDisableWakeupSources(void)
{
#if defined(PWR_CR3_EIWF_Msk)
  CLEAR_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#endif
}

/**
 * @brief Enable internal RTC wake sources only for timed PresTag states.
 *
 * @param[in] state Current state-machine state.
 * @param[in] is_active Current activity flag; unused by PresTag.
 * @return true because PresTag has no external wake pin polarity to verify.
 */
bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active)
{
  (void)is_active;

#if defined(PWR_CR3_EIWF_Msk)
  if ((state == RUNNING) || (state == CONFIGURED) || (state == HIBERNATING))
    SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#else
  (void)state;
#endif
  return true;
}
