#include "power.h"

#include "core_sync.h"
#include "gpio_utils.h"

/*
 * Polling SPI byte transfers shared by simple peripheral drivers.
 *
 * Existing tag code drives SPI peripheral registers directly rather than
 * through ChibiOS SPI transactions, so these helpers preserve that behavior
 * while centralizing the repeated full-duplex drain/read loops. This file also
 * owns SPI bus-session pin state, shared peripheral configuration,
 * active-state tracking, and bus mutex for descriptor-backed SPI devices.
 *
 * Storage flash drivers can be more sensitive to command/address/data pacing.
 * They should use storage_spi.h when they need byte-at-a-time transfers with
 * chip select held across a complete flash transaction.
 */

static bool spi1_on = false;
static bool spi1_suspended_for_stop = false;

/*
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
 */
bool isSpi1On(void)
{
  return spi1_on;
}

void tagMarkSpi1On(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  spi1_on = true;
#endif
}

void tagMarkSpi1Off(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  spi1_on = false;
#endif
}

void tagSpiDisableActiveForStop(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  if (spi1_on)
  {
    SPI1->CR1 &= ~SPI_CR1_SPE;
    spi1_suspended_for_stop = true;
  }
#endif
}

void tagSpiEnableActiveAfterStop(void)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  if (spi1_suspended_for_stop)
  {
    SPI1->CR1 |= SPI_CR1_SPE;
    spi1_suspended_for_stop = false;
  }
#endif
}

#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = SPI_CR1_MSTR,
    .cr2 = SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 |
           SPI_CR2_DS_1 | SPI_CR2_DS_0,
};

#else
const TagSpiConfig tagSpiDefaultConfig = {
    .cr1 = 0,
    .cr2 = 0,
};
#endif

/*
 * Generic bus-device adapter.
 *
 * TagRegisterDevice stores a TagBusDevice so register drivers can open a bus
 * without knowing whether the concrete transport is SPI, USART, or I2C.
 */
static void tagSpiBusOpsPowerOn(const void *device)
{
  tagSpiDevicePowerOn((const TagSpiDevice *)device);
}

static void tagSpiBusOpsPowerOff(const void *device)
{
  tagSpiDevicePowerOff((const TagSpiDevice *)device);
}

static void tagSpiBusOpsBegin(const void *device)
{
  tagSpiBusBegin((const TagSpiDevice *)device);
}

static void tagSpiBusOpsEnd(const void *device)
{
  tagSpiBusEnd((const TagSpiDevice *)device);
}

static void tagSpiBusOpsPrepareSleep(const void *device)
{
  tagSpiDevicePrepareSleep((const TagSpiDevice *)device);
}

const TagBusOps tagSpiBusOps = {
    .power_on = tagSpiBusOpsPowerOn,
    .power_off = tagSpiBusOpsPowerOff,
    .bus_begin = tagSpiBusOpsBegin,
    .bus_end = tagSpiBusOpsEnd,
    .prepare_sleep = tagSpiBusOpsPrepareSleep,
};

static void tagSpiDeviceEnable(const TagSpiDevice *device)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  const TagSpiConfig *config = device->config;
  if (!config)
  {
    config = &tagSpiDefaultConfig;
  }

  if (device->spi == SPI1)
  {
    rccEnableSPI1(0);
    rccResetSPI1();
  }

  device->spi->CR1 = 0;
  device->spi->CR2 = config->cr2;
  device->spi->CR1 = config->cr1;
  device->spi->CR1 |= SPI_CR1_SPE;

  if (device->spi == SPI1)
  {
    tagMarkSpi1On();
  }
#else
  (void)device;
#endif
}

static void tagSpiDeviceDisable(const TagSpiDevice *device)
{
#if defined(STM32_HAS_SPI1) && STM32_HAS_SPI1
  device->spi->CR1 = 0;
  device->spi->CR2 = 0;

  if (device->spi == SPI1)
  {
    tagMarkSpi1Off();
  }
#else
  (void)device;
#endif
}

/*
 * Device power and bus sessions.
 *
 * Power on/off handles optional switched power and safe idle pins. Bus
 * begin/end owns the shared mutex, alternate functions, and peripheral enable
 * state for one transaction/session.
 */
void tagSpiDevicePowerOn(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }

  palSetLine(device->cs);
  toOutput(device->cs);
}

void tagSpiDevicePowerOff(const TagSpiDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palClearLine(device->pwr);
  }

  toAnalog(device->sck);
  toAnalog(device->mosi);
  toAnalog(device->miso);
  toAnalog(device->cs);
}

void tagSpiBusBegin(const TagSpiDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->miso);
  toAlternate(device->mosi);

  tagSpiDeviceEnable(device);
}

void tagSpiBusEnd(const TagSpiDevice *device)
{
  palSetLine(device->cs);

  tagSpiDeviceDisable(device);

  toAnalog(device->sck);
  toAnalog(device->mosi);
  toAnalog(device->miso);

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

void tagSpiDevicePrepareSleep(const TagSpiDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_SPI_SLEEP_SAFE_IDLE:
    tagEnableStandbyPullup(device->cs);
    tagEnableStandbyPulldown(device->sck);
    tagEnableStandbyPulldown(device->mosi);
    break;

  case TAG_SPI_SLEEP_FLOAT:
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_SPI_SLEEP_CUSTOM:
    break;
  }
}

/*
 * Raw byte transfers.
 *
 * The default read/write routines intentionally use byte-at-a-time
 * request/response loops. Pipelined variants remain available for cases that
 * have been proven safe on hardware.
 */
void tagSpiWritePipelined(const TagSpiDevice *device, const uint8_t *buf,
                          uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = *buf++;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      (void)*spidr;
      read_len--;
    }
  }
}

void tagSpiReadPipelined(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;
  uint32_t read_len = len;

  while (len || read_len) {
    while (len && (spi->SR & SPI_SR_TXE)) {
      *spidr = device->dummy;
      len--;
    }
    while (read_len && (spi->SR & SPI_SR_RXNE)) {
      *buf++ = *spidr;
      read_len--;
    }
  }
}

void tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--)
  {
    *spidr = *buf++;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    (void)*spidr;
  }
}

void tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len)
{
  SPI_TypeDef *spi = tagSpiDevicePeripheral(device);
  volatile uint8_t *spidr = (volatile uint8_t *)&spi->DR;

  while (len--)
  {
    *spidr = device->dummy;
    while ((spi->SR & SPI_SR_RXNE) == 0)
    {
      ;
    }
    *buf++ = *spidr;
  }
}

void tagSpiSelect(const TagSpiDevice *device)
{
  palClearLine(device->cs);
}

void tagSpiDeselect(const TagSpiDevice *device)
{
  palSetLine(device->cs);
}
