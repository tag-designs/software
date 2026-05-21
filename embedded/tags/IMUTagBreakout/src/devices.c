#include "hal.h"

#include "app.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
#include "power.h"
#include "storage_mx25l.h"

/*
 * IMUTagBreakout device bindings.
 *
 * This target predates the descriptor-driven device model, so keep the local
 * SPI sequencing intact while moving it out of pwr.c. That lets pwr.c focus on
 * RTC and standby entry, matching the newer PresTag/CompassTag split.
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
  tagMarkSpi1On();
}

static void spiDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
  tagMarkSpi1Off();
}

void magOn(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_AK_CS);
  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();
}

void magOff(void)
{
  palSetLine(LINE_AK_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

void lpsOn(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_LPS_CS);
  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LPS_MOSI);

  spiEnable();
}

void lpsOff(void)
{
  palSetLine(LINE_LPS_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LPS_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

void lsm6On(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_LSM_CS);
  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LSM_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();
}

void lsm6Off(void)
{
  palSetLine(LINE_LSM_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LSM_MISO);
  chBSemSignal(&SPImutex);
}

static const TagSpiDevice external_flash_power = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_MX_nCS,
    .sck = LINE_MX_SCK,
    .miso = LINE_MX_MISO,
    .mosi = LINE_MX_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = &mx25lStorageOps,
    .spi = &external_flash_power,
    .sector_size = MX25L_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / MX25L_SECTOR_SIZE,
};

void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_LSM_CS);
  tagEnableStandbyPullup(LINE_AK_CS);
  tagEnableStandbyPullup(LINE_LPS_CS);
  tagEnableStandbyPulldown(LINE_LSM_MOSI);
  tagEnableStandbyPulldown(LINE_LSM_CK);

  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}
