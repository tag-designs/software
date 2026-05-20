#include "hal.h"

#include "custom.h"
#include "device.h"
#include "devices.h"
#include "lps.h"
#include "power.h"
#include "sensor_io.h"
#include "storage_device.h"
#include "storage_flash.h"
#include "test_support.h"
#include "timekeeping.h"

#if defined(TAG_FLASH_AT25XE)
#include "at25xe.h"
#define EXTERNAL_FLASH_OPS (&at25xeStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE AT25XE_SECTOR_SIZE
#elif defined(TAG_FLASH_MX25R)
#include "mx25r.h"
#define EXTERNAL_FLASH_OPS (&mx25rStorageOps)
#define EXTERNAL_FLASH_SECTOR_SIZE MX25R_SECTOR_SIZE
#else
#error "BitPresTag family requires a supported external flash module"
#endif

/*
 * BitPresTag-family board bindings for shared external-flash storage code.
 *
 * BitPresTag and BitPresTagMX25R share board wiring and differ only in the
 * flash module selected by project.mk. The selected module chooses the chip
 * operation table above; this family descriptor supplies the common board
 * wiring, enable hooks, and geometry.
 */
static const TagSpiDevice external_flash_power = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

const TagStorageDevice tagExternalFlash = {
    .ops = EXTERNAL_FLASH_OPS,
    .spi = &external_flash_power,
    .sector_size = EXTERNAL_FLASH_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / EXTERNAL_FLASH_SECTOR_SIZE,
};

bool tag_test_external_flash(void)
{
  tagStorageWake(TAG_EXTERNAL_FLASH);
  bool result = tagStorageCheckID(TAG_EXTERNAL_FLASH) > -1;
  tagStorageSleep(TAG_EXTERNAL_FLASH);
  return result;
}

void tagDevicesPrepareStandby(uint32_t state)
{
  tagStoragePrepareStandby(TAG_EXTERNAL_FLASH, state);
}

void tagDevicesApplyStandbyPins(void)
{
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagStorageApplyStandbyPins(TAG_EXTERNAL_FLASH);
}

#if defined(LPS_USART)
static const TagUsartDevice lps_usart_device = {
    .controller = &tagUsart2SyncController,
    .config = &tagUsart2SyncDefaultConfig,
    .cs = LINE_LPS_CS,
    .sck = LINE_LPS_SCK,
    .tx = LINE_LPS_TX,
    .rx = LINE_LPS_RX,
    .pwr = LINE_LPS_PWR,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_FLOAT,
};

static const TagStUsartRegisterBus lps_register_usart = {
    .device = &lps_usart_device,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus lps_registers = {
    .read_register = tagStUsartReadRegister,
    .write_register = tagStUsartWriteRegister,
    .context = &lps_register_usart,
};

static void lpsSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

const TagPressureDevice tagBitPresTagPressureDevice = {
    .registers = &lps_registers,
    .sleep_ms = lpsSleepMilliseconds,
};

bool tag_test_lps27(void)
{
  return tagPressureTest();
}
#endif

#if defined(TAG_SENSOR_ACCEL_ADXL362)
static const TagSpiDevice accel_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .miso = LINE_ACCEL_MISO,
    .mosi = LINE_ACCEL_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

void accelSpiOn(void)
{
  tagSpiBusBegin(&accel_bus);
}

void accelSpiOff(void)
{
  tagSpiBusEnd(&accel_bus);
}

void accelBusBegin(void)
{
  accelSpiOn();
}

void accelBusEnd(void)
{
  accelSpiOff();
}
#endif
