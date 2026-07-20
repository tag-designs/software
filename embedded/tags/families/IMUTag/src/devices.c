/**
 * @file devices.c
 * @brief IMUTagBreakout device descriptors, legacy SPI helpers, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
#include "bmm350_tag.h"
#include "i2c_bus.h"
#if defined(TAG_IMUTAG_RTC_I2C_HARDWARE) && TAG_IMUTAG_RTC_I2C_HARDWARE
#include "rtc_device.h"
#include "rv3028.h"
#endif
#else
#include "ak09940a.h"
#endif
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
#include "test_support.h"

#if defined(TAG_FLASH_GD5F1GQ5RE) && TAG_FLASH_GD5F1GQ5RE
#include "storage_gd5f.h"
#define EXTERNAL_FLASH_OPS (&gd5fStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE GD5F_BLOCK_SIZE
#define EXTERNAL_FLASH_SECTOR_COUNT GD5F_LOGICAL_BLOCK_COUNT
#elif defined(TAG_FLASH_MX25U12843) && TAG_FLASH_MX25U12843
#include "storage_mx25u12843.h"
#define EXTERNAL_FLASH_OPS (&mx25u12843StorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25U12843_SECTOR_SIZE
#define EXTERNAL_FLASH_SECTOR_COUNT MX25U12843_SECTOR_COUNT
#else
#include "storage_mx25l.h"
#define EXTERNAL_FLASH_OPS (&mx25lStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25L_SECTOR_SIZE
#define EXTERNAL_FLASH_SECTOR_COUNT (EXT_FLASH_SIZE / MX25L_SECTOR_SIZE)
#endif

#define LPTIM2_ALTERNATE_FUNCTION 14
#define IMUTAG_I2C_TIMEOUT 100

#ifndef BMM350_I2C_ADDRESS
#define BMM350_I2C_ADDRESS 0x14U
#endif

#ifndef IMUTAG_FLASH_CS_LINE
#define IMUTAG_FLASH_CS_LINE LINE_FLASH_nCS
#endif
#ifndef IMUTAG_FLASH_SCK_LINE
#define IMUTAG_FLASH_SCK_LINE LINE_FLASH_SCK
#endif
#ifndef IMUTAG_FLASH_MISO_LINE
#define IMUTAG_FLASH_MISO_LINE LINE_FLASH_MISO
#endif
#ifndef IMUTAG_FLASH_MOSI_LINE
#define IMUTAG_FLASH_MOSI_LINE LINE_FLASH_MOSI
#endif

#ifndef IMUTAG_LPS_CS_LINE
#define IMUTAG_LPS_CS_LINE LINE_LPS_CS
#endif
#ifndef IMUTAG_LPS_SCK_LINE
#define IMUTAG_LPS_SCK_LINE LINE_ACCEL_LPS_MAG_SCK
#endif
#ifndef IMUTAG_LPS_MISO_LINE
#define IMUTAG_LPS_MISO_LINE LINE_LPS_MAG_MISO
#endif
#ifndef IMUTAG_LPS_MOSI_LINE
#define IMUTAG_LPS_MOSI_LINE LINE_LPS_MOSI
#endif
#ifndef IMUTAG_LPS_DRDY_LINE
#define IMUTAG_LPS_DRDY_LINE LINE_LPS_DRDY
#endif

#ifndef IMUTAG_IMU_CS_LINE
#define IMUTAG_IMU_CS_LINE LINE_ACCEL_CS
#endif
#ifndef IMUTAG_IMU_SCK_LINE
#define IMUTAG_IMU_SCK_LINE LINE_ACCEL_LPS_MAG_SCK
#endif
#ifndef IMUTAG_IMU_MISO_LINE
#define IMUTAG_IMU_MISO_LINE LINE_ACCEL_MISO
#endif
#ifndef IMUTAG_IMU_MOSI_LINE
#define IMUTAG_IMU_MOSI_LINE LINE_ACCEL_MAG_MOSI
#endif
#ifndef IMUTAG_IMU_TRIGGER_LINE
#define IMUTAG_IMU_TRIGGER_LINE LINE_ACCEL_TRG
#endif

#ifndef IMUTAG_BMM_DRDY_LINE
#define IMUTAG_BMM_DRDY_LINE LINE_IMU_TRG_TEST
#endif

#ifndef IMUTAG_MAG_CS_LINE
#define IMUTAG_MAG_CS_LINE LINE_MAG_CS
#endif
#ifndef IMUTAG_MAG_SCK_LINE
#define IMUTAG_MAG_SCK_LINE LINE_ACCEL_LPS_MAG_SCK
#endif
#ifndef IMUTAG_MAG_MISO_LINE
#define IMUTAG_MAG_MISO_LINE LINE_LPS_MAG_MISO
#endif
#ifndef IMUTAG_MAG_MOSI_LINE
#define IMUTAG_MAG_MOSI_LINE LINE_ACCEL_MAG_MOSI
#endif
#ifndef IMUTAG_MAG_TRIGGER_LINE
#define IMUTAG_MAG_TRIGGER_LINE LINE_MAG_TRG
#endif

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
#if !defined(TAG_RTC_I2C_DELAY_CYCLES)
#define TAG_RTC_I2C_DELAY_CYCLES 1U
#endif

#if defined(TAG_IMUTAG_RTC_I2C_HARDWARE) && TAG_IMUTAG_RTC_I2C_HARDWARE
#ifndef TAG_IMUTAG_I2C_TIMINGR
#define TAG_IMUTAG_I2C_TIMINGR 0x00000509U
#endif

static binary_semaphore_t imutag_rtc_i2c_mutex;

static const I2CConfig imutag_rtc_i2c_config = {
    TAG_IMUTAG_I2C_TIMINGR,
    0,
    0,
};

const TagI2cController tagRtcI2cController = {
    .backend = TAG_I2C_BACKEND_HARDWARE,
    .mutex = &imutag_rtc_i2c_mutex,
    .driver.hardware = &I2CD1,
};

static const TagI2cDevice imutag_rtc_i2c = {
    .controller = &tagRtcI2cController,
    .config.hardware = &imutag_rtc_i2c_config,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
    .address = RV3028_ADR,
    .timeout = IMUTAG_I2C_TIMEOUT,
    .sleep_policy = TAG_I2C_SLEEP_PULLUP,
};

static const TagRtcDevice imutag_rtc_device = {
    .registers = &imutag_rtc_i2c,
    .device_on = rtcOn,
    .device_off = rtcOff,
};

const TagRtcDevice *tagRtcDevice(void)
{
  return &imutag_rtc_device;
}

void tagRtcDeviceRuntimeInit(void)
{
  chBSemObjectInit(&imutag_rtc_i2c_mutex, false);
  tagI2cControllerObjectInit(&tagRtcI2cController);
}
#else
extern const TagI2cController tagRtcI2cController;

static void imutagBmm350I2cDelay(void)
{
#if TAG_RTC_I2C_DELAY_CYCLES == 1U
  __NOP();
#else
  for (uint32_t i = 0; i < TAG_RTC_I2C_DELAY_CYCLES; i++) {
    __NOP();
  }
#endif
}

static const TagSoftI2cConfig bmm350_i2c_config = {
    .delay = imutagBmm350I2cDelay,
#if defined(SWAP_I2C) && SWAP_I2C
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA,
#else
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
#endif
};
#endif

static const TagI2cDevice bmm350_i2c = {
    .controller = &tagRtcI2cController,
#if defined(TAG_IMUTAG_RTC_I2C_HARDWARE) && TAG_IMUTAG_RTC_I2C_HARDWARE
    .config.hardware = &imutag_rtc_i2c_config,
#else
    .config.software = &bmm350_i2c_config,
#endif
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
    .address = BMM350_I2C_ADDRESS,
    .timeout = IMUTAG_I2C_TIMEOUT,
    .sleep_policy = TAG_I2C_SLEEP_PULLUP,
};

static TagBmm350Compensation bmm350_compensation;

const TagBmm350Device tagImuTagBmm350Device = {
    .i2c = &bmm350_i2c,
    .drdy = IMUTAG_BMM_DRDY_LINE,
    .compensation = &bmm350_compensation,
    .interrupt_polarity = BMM350_INT_ACTIVE_HIGH,
    .interrupt_drive = BMM350_INT_PUSH_PULL,
};
#endif

/*
 * IMUTagBreakout device bindings.
 *
 * This target predates the descriptor-driven device model, so keep the local
 * SPI sequencing intact while moving it out of pwr.c. That lets pwr.c focus on
 * RTC and standby entry, matching the newer PresTag/CompassTag split.
 */


static void tagImuTagDisableTriggerClock(void)
{
  RCC->APB1ENR2 &= ~RCC_APB1ENR2_LPTIM2EN;
#if defined(RCC_APB1SMENR2_LPTIM2SMEN)
  RCC->APB1SMENR2 &= ~RCC_APB1SMENR2_LPTIM2SMEN;
#endif
#if defined(RCC_APB1SLPENR2_LPTIM2SLPEN)
  RCC->APB1SLPENR2 &= ~RCC_APB1SLPENR2_LPTIM2SLPEN;
#endif
#if defined(RCC_APB1STPENR2_LPTIM2STPEN)
  RCC->APB1STPENR2 &= ~RCC_APB1STPENR2_LPTIM2STPEN;
#endif
#if defined(RCC_APB1AMENR2_LPTIM2AMEN)
  RCC->APB1AMENR2 &= ~RCC_APB1AMENR2_LPTIM2AMEN;
#endif
}

static void tagImuTagEnableTriggerClock(void)
{
  RCC->APB1ENR2 |= RCC_APB1ENR2_LPTIM2EN;
#if defined(RCC_APB1SMENR2_LPTIM2SMEN)
  RCC->APB1SMENR2 |= RCC_APB1SMENR2_LPTIM2SMEN;
#endif
#if defined(RCC_APB1SLPENR2_LPTIM2SLPEN)
  RCC->APB1SLPENR2 |= RCC_APB1SLPENR2_LPTIM2SLPEN;
#endif
#if defined(RCC_APB1STPENR2_LPTIM2STPEN)
  RCC->APB1STPENR2 |= RCC_APB1STPENR2_LPTIM2STPEN;
#endif
#if defined(RCC_APB1AMENR2_LPTIM2AMEN)
  RCC->APB1AMENR2 |= RCC_APB1AMENR2_LPTIM2AMEN;
#endif
}

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = IMUTAG_FLASH_CS_LINE,
        .sck = IMUTAG_FLASH_SCK_LINE,
        .miso = IMUTAG_FLASH_MISO_LINE,
        .mosi = IMUTAG_FLASH_MOSI_LINE,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXTERNAL_FLASH_SECTOR_COUNT,
};

static const TagRegisterDevice lps_registers = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = IMUTAG_LPS_CS_LINE,
        .sck = IMUTAG_LPS_SCK_LINE,
        .miso = IMUTAG_LPS_MISO_LINE,
        .mosi = IMUTAG_LPS_MOSI_LINE,
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
        .cs = IMUTAG_IMU_CS_LINE,
        .sck = IMUTAG_IMU_SCK_LINE,
        .miso = IMUTAG_IMU_MISO_LINE,
        .mosi = IMUTAG_IMU_MOSI_LINE,
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
  palClearLine(IMUTAG_IMU_TRIGGER_LINE);
  palSetLineMode(IMUTAG_IMU_TRIGGER_LINE, PAL_MODE_INPUT_ANALOG);

  if (divider == 0U) {
    tagImuTagDisableTriggerClock();
    debug_log_printf("IMUTag trigger: disabled\r\n");
    return;
  }

  if (divider < 2U)
    divider = 2U;

  tagImuTagEnableTriggerClock();
  RCC->APB1RSTR2 |= RCC_APB1RSTR2_LPTIM2RST;
  RCC->APB1RSTR2 &= ~RCC_APB1RSTR2_LPTIM2RST;

  palSetLineMode(IMUTAG_IMU_TRIGGER_LINE,
                 PAL_MODE_ALTERNATE(LPTIM2_ALTERNATE_FUNCTION));

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

#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
const TagRegisterDevice tagImuTagMagDevice = {
    .kind = TAG_REGISTER_ST,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = IMUTAG_MAG_CS_LINE,
        .sck = IMUTAG_MAG_SCK_LINE,
        .miso = IMUTAG_MAG_MISO_LINE,
        .mosi = IMUTAG_MAG_MOSI_LINE,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};
#endif

static const TagTestCase tag_tests[] =
{
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
#if defined(TAG_FLASH_MX25U12843) && TAG_FLASH_MX25U12843
  {RUN_MX25U12843, tag_test_external_flash, TAG_EXTERNAL_FLASH},
#endif
  {RUN_AIS2, tag_test_lsm6dsv16x, TAG_IMU_DEVICE},
  {RUN_LPS, tag_test_lps22hh, TAG_PRESSURE_DEVICE},
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  {RUN_MMC5633, tag_test_bmm350, TAG_MAG_DEVICE}
#else
  {RUN_MMC5633, tag_test_ak09940a, TAG_MAG_DEVICE}
#endif
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

#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
/**
 * @brief Return the IMUTagBreakout AK09940A descriptor.
 *
 * @return Magnetometer register descriptor used by shared AK09940A code.
 */
const TagRegisterDevice *tagAk09940aDevice(void)
{
  return TAG_MAG_DEVICE;
}
#endif

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
     palSetLineMode(IMUTAG_LPS_DRDY_LINE, PAL_MODE_INPUT_ANALOG);
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
     palSetLineMode(IMUTAG_BMM_DRDY_LINE, PAL_MODE_INPUT_ANALOG);
#else
     palSetLineMode(IMUTAG_MAG_TRIGGER_LINE, PAL_MODE_INPUT_ANALOG);
#endif
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
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  tagI2cDevicePrepareSleep(TAG_MAG_DEVICE->i2c);
#else
  tagBusPrepareSleep(&tagImuTagMagDevice.bus);
#endif
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
