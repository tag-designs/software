#include "hal.h"

#include "custom.h"
#include "gpio_utils.h"
#include "lis2du12.h"
#include "power.h"
#include "storage_device.h"

#if defined(TAG_FLASH_AT25XE)
#include "at25xe.h"
#define EXTERNAL_FLASH_OPS (&at25xeStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE AT25XE_SECTOR_SIZE
#elif defined(TAG_FLASH_MX25R)
#include "mx25r.h"
#define EXTERNAL_FLASH_OPS (&mx25rStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25R_SECTOR_SIZE
#else
#error "CompassTag family requires a supported external flash module"
#endif

/*
 * CompassTag-family device bindings that are not yet general tag drivers.
 *
 * The LIS2DU12 code in this family is tailored to the CompassTag USART wiring
 * and sampling needs, so keep its TagLis2du12Device descriptor beside the
 * family code instead of burying it in the shared power file.
 */

static const TagSpiBus external_flash_spi_bus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .dummy = 0xff,
};

static const TagSpiDevice external_flash_power = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .spi = &external_flash_spi_bus,
    .power = &external_flash_power,
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / EXTERNAL_FLASH_SECTOR_SIZE,
};

#ifdef ACCEL_USART
static const TagUsartBus accel_usart_bus = {
    .usart = USART2,
    .config = &tagUsart2SyncDefaultConfig,
    .cs = LINE_ACCEL_CS,
    .dummy = 0xff,
};

static const TagStUsartRegisterBus accel_register_usart = {
    .bus = &accel_usart_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus accel_registers = {
    .read_register = tagStUsartReadRegister,
    .write_register = tagStUsartWriteRegister,
    .context = &accel_register_usart,
};

void accelOn(void)
{
  palSetLine(LINE_ACCEL_CS);
  toOutput(LINE_ACCEL_CS);

  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_ACCEL_TX);
  toAlternate(LINE_ACCEL_RX);

  tagUsart2SyncEnable(accel_usart_bus.config);
}

void accelOff(void)
{
#ifdef COMPASS_TAG
  palSetLine(LINE_ACCEL_CS);
  tagUsart2SyncDisable();
  toOutput(LINE_ACCEL_SCK);
  toOutput(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
#else
  tagUsart2SyncDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
#endif
}

static void accelWriteRegisterByte(const void *context, uint8_t reg,
                                   uint8_t val)
{
  const TagUsartBus *bus = (const TagUsartBus *)context;
  uint8_t buffer[] = {reg, val};

  tagUsartBusWrite(bus, buffer, sizeof(buffer));
}

static const TagLis2du12Device compass_tag_accel = {
    .registers = &accel_registers,
    .bus_begin = accelOn,
    .bus_end = accelOff,
    .write_register_byte = accelWriteRegisterByte,
    .write_register_byte_context = &accel_usart_bus,
};

const TagLis2du12Device *tagLis2du12Device(void)
{
  return &compass_tag_accel;
}

void tagPrepareDevicesForStandby(void)
{
#if defined(TAG_SENSOR_MAG_AK09940A) && defined(COMPASS_TAG)
  tagEnableStandbyPulldown(LINE_MAG_CS);
  tagEnableStandbyPulldown(LINE_MAG_SCK);
  tagEnableStandbyPulldown(LINE_MAG_MOSI);
#endif

  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagEnableStandbyPulldown(LINE_ACCEL_TX);
  tagEnableStandbyPulldown(LINE_ACCEL_SCK);
}
#endif
