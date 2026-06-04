/**
 * @file devices.c
 * @brief IMUTagBreakout device descriptors, legacy SPI helpers, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "ak09940a.h"
#include "app.h"
#include "core_sync.h"
#include "custom.h"
#include "debug_log.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
#include "lsm6dsv16x.h"
#include "lps.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_mx25l.h"
#include "test_support.h"

/*
 * IMUTagBreakout device bindings.
 *
 * This target predates the descriptor-driven device model, so keep the local
 * SPI sequencing intact while moving it out of pwr.c. That lets pwr.c focus on
 * RTC and standby entry, matching the newer PresTag/CompassTag split.
 */

/**
 * @brief Enable SPI1 using the legacy IMUTagBreakout register sequence.
 */
static void spiEnable(void)
{
  rccEnableSPI1(0);
  rccResetSPI1();

  SPI1->CR1 = 0;
  SPI1->CR2 =
      SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 |
      SPI_CR2_DS_1 | SPI_CR2_DS_0;

  SPI1->CR1 = SPI_CR1_MSTR;
  SPI1->CR1 |= SPI_CR1_SPE;
  tagMarkSpiOn(SPI1);
}

/**
 * @brief Disable SPI1 after a legacy local-device transaction.
 */
static void spiDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
  tagMarkSpiOff(SPI1);
}

/**
 * @brief Acquire SPI and configure pins for the legacy magnetometer path.
 */
void magOn(void)
{
  chBSemWait(&SPI1mutex);

  palSetLine(LINE_MAG_CS);
  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_ACCEL_MOSI);

  spiEnable();
}

/**
 * @brief Release SPI and float pins after the legacy magnetometer path.
 */
void magOff(void)
{
  palSetLine(LINE_MAG_CS);
  spiDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_ACCEL_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPI1mutex);
}

/**
 * @brief Acquire SPI and configure pins for the legacy pressure path.
 */
void lpsOn(void)
{
  chBSemWait(&SPI1mutex);

  palSetLine(LINE_LPS_CS);
  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LPS_MOSI);

  spiEnable();
}

/**
 * @brief Release SPI and float pins after the legacy pressure path.
 */
void lpsOff(void)
{
  palSetLine(LINE_LPS_CS);
  spiDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_LPS_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPI1mutex);
}

/**
 * @brief Acquire SPI and configure pins for the legacy LSM6 path.
 */
void lsm6On(void)
{
  chBSemWait(&SPI1mutex);

  palSetLine(LINE_ACCEL_CS);
  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_ACCEL_MISO);
  toAlternate(LINE_ACCEL_MOSI);

  spiEnable();
}

/**
 * @brief Release SPI and float pins after the legacy LSM6 path.
 */
void lsm6Off(void)
{
  palSetLine(LINE_ACCEL_CS);
  spiDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_ACCEL_MOSI);
  toAnalog(LINE_ACCEL_MISO);
  chBSemSignal(&SPI1mutex);
}

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
        .sck = LINE_ACCEL_SCK,
        .miso = LINE_LPS_MISO,
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
        .sck = LINE_ACCEL_SCK,
        .miso = LINE_ACCEL_MISO,
        .mosi = LINE_ACCEL_MOSI,
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
  toAnalog(LINE_ACCEL_TRG);
  palClearLine(LINE_IMU_TRG_TEST);
  toAnalog(LINE_IMU_TRG_TEST);

  if (divider == 0U) {
    RCC->APB1ENR2 &= ~RCC_APB1ENR2_LPTIM2EN;
    debug_log_printf("IMUTag trigger: disabled\r\n");
    return;
  }

  if (divider < 2U)
    divider = 2U;

  RCC->APB1ENR2 |= RCC_APB1ENR2_LPTIM2EN;
  RCC->APB1RSTR2 |= RCC_APB1RSTR2_LPTIM2RST;
  RCC->APB1RSTR2 &= ~RCC_APB1RSTR2_LPTIM2RST;

  toAlternate(LINE_ACCEL_TRG);

  LPTIM2->CFGR = 0U;
  LPTIM2->CR = STM32_LPTIM_CR_ENABLE;

  LPTIM2->ARR = divider - 1U;
  LPTIM2->CMP = divider / 2U;
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
        .sck = LINE_ACCEL_SCK,
        .miso = LINE_LPS_MISO,
        .mosi = LINE_ACCEL_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagTestCase tag_tests[] =
{
  /*
   * The monitor protocol still exposes RUN_MMC5633 for magnetometer tests.
   * IMUTagBreakout maps that legacy request to its AK09940 test hook.
   */
  {RUN_RTC, tag_test_rtc, NULL},
  {RUN_EXT_FLASH, tag_test_external_flash, TAG_EXTERNAL_FLASH},
  {RUN_AIS2, tag_test_lsm6dsv16x, TAG_IMU_DEVICE},
  {RUN_LPS, tag_test_lps22hh, TAG_PRESSURE_DEVICE},
  {RUN_MMC5633, tag_test_ak09940a, TAG_MAG_DEVICE}
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
 * @brief Prepare IMUTagBreakout devices before entering standby.
 *
 * @param[in] state Current state-machine state.
 */
void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
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
