/**
 * @file devices.c
 * @brief IMUTagBreakout device descriptors, legacy SPI helpers, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "ak09940a.h"
#include "lps22hh.h"
#include "app.h"
#include "core_sync.h"
#include "custom.h"
#include "debug_log.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
#include "lsm6dsv16x.h"
#include "lps.h"
#include "persistent.h"
#include "power.h"
#include "sensor_io.h"
#include "sensors.h"
#include "storage_mx25l.h"
#include "test_support.h"

#define LPTIM2_ALTERNATE_FUNCTION 14

/*
 * IMUTagBreakout device bindings.
 *
 * This target predates the descriptor-driven device model, so keep the local
 * SPI sequencing intact while moving it out of pwr.c. That lets pwr.c focus on
 * RTC and standby entry, matching the newer PresTag/CompassTag split.
 */


const TagStorageDevice tagExternalFlash = {
    .ops = &mx25lStorageOps,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_FLASH_nCS,
        .sck = LINE_FLASH_SCK,
        .miso = LINE_FLASH_MISO,
        .mosi = LINE_FLASH_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = MX25L_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / MX25L_SECTOR_SIZE,
};

static const TagRegisterDevice lps_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_LPS_CS,
        .sck = LINE_ACCEL_LPS_MAG_SCK,
        .miso = LINE_LPS_MAG_MISO,
        .mosi = LINE_LPS_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

const TagPressureDevice tagImuTagPressureDevice = {
    .registers = &lps_registers,
};

static const TagRegisterDevice imu_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_ACCEL_CS,
        .sck = LINE_ACCEL_LPS_MAG_SCK,
        .miso = LINE_ACCEL_MISO,
        .mosi = LINE_ACCEL_MAG_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

/**
 * @brief Configure PA8/LPTIM2_OUT as the IMU external ODR trigger clock.
 *
 * LPTIM2 is clocked from the 1024 Hz low-speed source selected in mcuconf.h.
 * The output frequency is 1024 / divider Hz and is routed to the IMU INT2 /
 * ACCEL_TRG line.
 */
void tagImuTagSetTrigger(unsigned int divider)
{
  palClearLine(LINE_ACCEL_TRG);
  palSetLineMode(LINE_ACCEL_TRG, PAL_MODE_INPUT_ANALOG);

  if (divider == 0U) {
    RCC->APB1ENR2 &= ~RCC_APB1ENR2_LPTIM2EN;
#if defined(RCC_APB1SMENR2_LPTIM2SMEN)
    RCC->APB1SMENR2 &= ~RCC_APB1SMENR2_LPTIM2SMEN;
#endif
    debug_log_printf("IMUTag trigger: disabled\r\n");
    return;
  }

  if (divider < 2U)
    divider = 2U;

  RCC->APB1ENR2 |= RCC_APB1ENR2_LPTIM2EN;
#if defined(RCC_APB1SMENR2_LPTIM2SMEN)
  RCC->APB1SMENR2 |= RCC_APB1SMENR2_LPTIM2SMEN;
#endif
  RCC->APB1RSTR2 |= RCC_APB1RSTR2_LPTIM2RST;
  RCC->APB1RSTR2 &= ~RCC_APB1RSTR2_LPTIM2RST;

  palSetLineMode(LINE_ACCEL_TRG, PAL_MODE_ALTERNATE(LPTIM2_ALTERNATE_FUNCTION));

  LPTIM2->CFGR = 0U;
  LPTIM2->CR = STM32_LPTIM_CR_ENABLE;

  LPTIM2->ARR = divider - 1U;
#if defined(LPTIM_CCR1_CCR1)
  LPTIM2->CCR1 = divider / 2U;
#else
  LPTIM2->CMP = divider / 2U;
#endif
  LPTIM2->CNT = 0U;
  LPTIM2->CR |= STM32_LPTIM_CR_CNTSTRT;

  debug_log_printf("IMUTag trigger: divider %u\r\n", divider);
}

static void imuSetTrigger(const void *context, unsigned int divider)
{
  (void)context;
  tagImuTagSetTrigger(divider);
}

const TagLsm6dsv16xDevice tagImuTagImuDevice = {
    .registers = &imu_registers,
    .set_trigger = imuSetTrigger,
    .trigger_context = NULL,
};

const TagRegisterDevice tagImuTagMagDevice = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_MAG_CS,
        .sck = LINE_ACCEL_LPS_MAG_SCK,
        .miso = LINE_LPS_MAG_MISO,
        .mosi = LINE_ACCEL_MAG_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagTestCase tag_tests[] =
{
  /* Keep legacy monitor request names accepted while exposing device-specific names. */
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_RV3028, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_MX25L, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_AIS2, tag_test_lsm6dsv16x, TAG_IMU_DEVICE},
  {RUN_LSM6DSV16X, tag_test_lsm6dsv16x, TAG_IMU_DEVICE},
  {RUN_LPS, tag_test_lps22hh, TAG_PRESSURE_DEVICE},
  {RUN_LPS22HH, tag_test_lps22hh, TAG_PRESSURE_DEVICE},
  {RUN_MMC5633, tag_test_ak09940a, TAG_MAG_DEVICE},
  {RUN_AK09940A, tag_test_ak09940a, TAG_MAG_DEVICE}
};

/**
 * @brief Return the IMUTagBreakout self-test table.
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
 * @brief Return the IMUTagBreakout AK09940A descriptor.
 *
 * @return Magnetometer register descriptor used by shared AK09940A code.
 */
const TagRegisterDevice *tagAk09940aDevice(void)
{
  return TAG_MAG_DEVICE;
}

/**
 * @brief Apply IMUTagBreakout device power policy for a lifecycle phase.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
void tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state)
{
  switch (reason) {
  case TAG_DEVICE_POWER_STANDBY_ENTRY:
    tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
    if (state != TagState_RUNNING) {
     (void)deinitDataCollection();
     palSetLineMode(LINE_LPS_DRDY, PAL_MODE_INPUT_ANALOG);
     palSetLineMode(LINE_MAG_TRG, PAL_MODE_INPUT_ANALOG);
    }
    break;

  case TAG_DEVICE_POWER_BOOT_CLEANUP:
  case TAG_DEVICE_POWER_RUNTIME_DEINIT:
  case TAG_DEVICE_POWER_TERMINAL_ENTRY:
  default:
    (void)deinitDataCollection();
    break;
  }
}

/**
 * @brief Prepare IMUTagBreakout devices before entering standby.
 *
 * @param[in] state Current state-machine state.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, state);
}

/**
 * @brief Deinitialize IMUTagBreakout-owned device resources.
 */
void tagDevicesDeinit(void)
{
  tagDevicesApplyPowerState(TAG_DEVICE_POWER_RUNTIME_DEINIT, pState->state);
}

/**
 * @brief Apply board pin pulls needed for standby leakage.
 */
void tagDevicesApplyStandbyPins(void)
{
  /* Legacy static-board fallback; generated IMUTagv1 uses board_standby.h. */
  tagBusPrepareSleep(&tagImuTagMagDevice.bus);
  tagBusPrepareSleep(&imu_registers.bus);
  tagBusPrepareSleep(&lps_registers.bus);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}

/**
 * @brief Disable IMU wakeup sources before entering terminal standby.
 */
void tagDevicesDisableWakeupSources(void)
{
#if defined(PWR_CR3_EWUP1_Msk)
  CLEAR_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk);
#elif defined(PWR_WUCR1_WUPEN1)
  CLEAR_BIT(PWR->WUCR1, PWR_WUCR1_WUPEN1);
#endif
}

/**
 * @brief Configure wake sources for IMUTag standby.
 *
 * IMUTag uses EXTI on LINE_WKUP1 for Stop1 collection wakes. Terminal
 * standby states should not leave the IMU interrupt as a wakeup pin.
 */
bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active)
{
  (void)state;
  (void)is_active;

#if defined(PWR_CR3_EIWF_Msk)
  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#endif
  return true;
}
