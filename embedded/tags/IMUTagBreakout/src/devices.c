#include "hal.h"

#include "app.h"
#include "custom.h"
#include "device.h"
#include "external_flash.h"
#include "gpio_utils.h"
#include "power.h"

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

void FlashSpiOn(void)
{
  chBSemWait(&SPImutex);

  palSetLine(LINE_MX_nCS);
  toAlternate(LINE_MX_SCK);
  toAlternate(LINE_MX_MOSI);
  toAlternate(LINE_MX_MISO);

  spiEnable();
}

void FlashSpiOff(void)
{
  palSetLine(LINE_MX_nCS);
  spiDisable();
  toAnalog(LINE_MX_SCK);
  toAnalog(LINE_MX_MOSI);
  toAnalog(LINE_MX_MISO);
  chBSemSignal(&SPImutex);
}

void tagDevicesPrepareStandby(uint32_t state)
{
  if ((state == IDLE) ||
      (state == ABORTED) ||
      (state == FINISHED) ||
      (state == EXCEPTION) ||
      (state == HIBERNATING))
  {
    ExFlashPwrUp();
    ExFlashPwrDown();
  }
}

void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_LSM_CS);
  tagEnableStandbyPullup(LINE_AK_CS);
  tagEnableStandbyPullup(LINE_LPS_CS);
  tagEnableStandbyPulldown(LINE_LSM_MOSI);
  tagEnableStandbyPulldown(LINE_LSM_CK);

  tagEnableStandbyPullup(LINE_MX_nCS);
  tagEnableStandbyPulldown(LINE_MX_SCK);
  tagEnableStandbyPulldown(LINE_MX_MOSI);
}
