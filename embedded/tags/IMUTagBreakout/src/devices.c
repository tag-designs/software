/**
 * @file devices.c
 * @brief IMUTagBreakout device descriptors, legacy SPI helpers, and power hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "app.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
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
  chBSemWait(&SPImutex);

  palSetLine(LINE_AK_CS);
  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();
}

/**
 * @brief Release SPI and float pins after the legacy magnetometer path.
 */
void magOff(void)
{
  palSetLine(LINE_AK_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

/**
 * @brief Acquire SPI and configure pins for the legacy pressure path.
 */
void lpsOn(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_LPS_CS);
  toAlternate(LINE_LSM_CK);
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
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LPS_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

/**
 * @brief Acquire SPI and configure pins for the legacy LSM6 path.
 */
void lsm6On(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_LSM_CS);
  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LSM_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();
}

/**
 * @brief Release SPI and float pins after the legacy LSM6 path.
 */
void lsm6Off(void)
{
  palSetLine(LINE_LSM_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LSM_MISO);
  chBSemSignal(&SPImutex);
}

const TagStorageDevice tagExternalFlash = {
    .ops = &mx25lStorageOps,
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_MX_nCS,
        .sck = LINE_MX_SCK,
        .miso = LINE_MX_MISO,
        .mosi = LINE_MX_MOSI,
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
        .sck = LINE_LPS_SCK,
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

static const TagTestCase tag_tests[] =
{
  /*
   * The monitor protocol still exposes RUN_MMC5633 for magnetometer tests.
   * IMUTagBreakout maps that legacy request to its AK09940 test hook.
   */
  {RUN_MMC5633, AK09940A_FAILED, tag_test_ak09940a},
  {RUN_RTC, RTC_FAILED, tag_test_rtc},
  {RUN_EXT_FLASH, EXT_FLASH_FAILED, tag_test_external_flash},
  {RUN_LPS, LPS_FAILED, tag_test_lps22hh},
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
  tagEnableStandbyPullup(LINE_LSM_CS);
  tagEnableStandbyPullup(LINE_AK_CS);
  tagEnableStandbyPullup(LINE_LPS_CS);
  tagEnableStandbyPulldown(LINE_LSM_MOSI);
  tagEnableStandbyPulldown(LINE_LSM_CK);

  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}
