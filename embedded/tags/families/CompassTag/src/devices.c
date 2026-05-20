#include "hal.h"

#include "ak09940a.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "gpio_utils.h"
#include "lis2du12.h"
#include "power.h"
#include "storage_device.h"
#include "storage_flash.h"
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
#error "CompassTag family requires a supported external flash module"
#endif

/*
 * CompassTag-family device bindings that are not yet general tag drivers.
 *
 * The LIS2DU12 and AK09940A bindings are tailored to the CompassTag family
 * wiring and sampling path, so their descriptors live here instead of in the
 * shared power file.
 */

#if defined(LINE_MAG_CS) && defined(LINE_MAG_SCK) && defined(LINE_MAG_MISO) && defined(LINE_MAG_MOSI)
#define AK09940A_CS LINE_MAG_CS
#define AK09940A_SCK LINE_MAG_SCK
#define AK09940A_MISO LINE_MAG_MISO
#define AK09940A_MOSI LINE_MAG_MOSI
#else
#error "CompassTag AK09940A binding needs LINE_MAG_CS/SCK/MISO/MOSI aliases"
#endif

#if defined(LINE_MAG_PWR)
#define AK09940A_PWR LINE_MAG_PWR
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_FLOAT
#else
#define AK09940A_PWR TAG_NO_LINE
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_SAFE_IDLE
#endif

static const TagSpiDevice ak09940a_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = AK09940A_CS,
    .sck = AK09940A_SCK,
    .miso = AK09940A_MISO,
    .mosi = AK09940A_MOSI,
    .pwr = AK09940A_PWR,
    .dummy = 0xff,
    .sleep_policy = AK09940A_SLEEP_POLICY,
};

static const TagStSpiRegisterBus ak09940a_register_spi = {
    .device = &ak09940a_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus ak09940a_registers = {
    .read_register = tagStSpiReadRegister,
    .write_register = tagStSpiWriteRegister,
    .context = &ak09940a_register_spi,
};

void tagCompassMagResetAssert(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palClearLine(LINE_MAG_RSTN);
#endif
}

void tagCompassMagResetRelease(void)
{
#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
#endif
}

static void compassTagMagSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

#if defined(LINE_MAG_TRG)
#define AK09940A_TRG LINE_MAG_TRG
#endif

#if defined(AK09940A_TRG)
static void compassTagMagTriggerMode(bool output)
{
  if (output)
    toOutput(AK09940A_TRG);
  else
    toInput(AK09940A_TRG);
}

static void compassTagMagTrigger(void)
{
  palSetLine(AK09940A_TRG);
  palClearLine(AK09940A_TRG);
}

static bool compassTagMagDataReadyLine(void)
{
  return palReadLine(AK09940A_TRG) == PAL_HIGH;
}
#endif

const TagMagDevice tagCompassTagMagDevice = {
    .registers = &ak09940a_registers,
    .power_on = 0,
    .power_off = 0,
    .bus_begin = 0,
    .bus_end = 0,
    .sleep_ms = compassTagMagSleepMilliseconds,
#if defined(AK09940A_TRG)
    .set_trigger_output = compassTagMagTriggerMode,
    .trigger = compassTagMagTrigger,
    .data_ready_line = compassTagMagDataReadyLine,
#else
    .set_trigger_output = 0,
    .trigger = 0,
    .data_ready_line = 0,
#endif
};

const TagMagDevice *tagAk09940aDevice(void)
{
  return &tagCompassTagMagDevice;
}

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

void tagDevicesPrepareStandby(uint32_t state)
{
  tagCompassMagResetAssert();
  tagStoragePrepareStandby(&tagExternalFlash, state);
}

void tagDevicesApplyStandbyPins(void)
{
#if defined(LINE_MAG_RSTN)
  tagEnableStandbyPulldown(LINE_MAG_RSTN);
#endif
  tagSpiDevicePrepareSleep(&ak09940a_bus);

#ifdef ACCEL_USART
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagEnableStandbyPulldown(LINE_ACCEL_TX);
  tagEnableStandbyPulldown(LINE_ACCEL_SCK);
#endif

  tagStorageApplyStandbyPins(&tagExternalFlash);
}

bool tag_test_ak09940a(void)
{
  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  if (TAG_MAG_DEVICE->sleep_ms)
    TAG_MAG_DEVICE->sleep_ms(1);
  tagCompassMagResetRelease();
  bool ok = ak09940aCheckWhoami(TAG_MAG_DEVICE);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);
  tagCompassMagResetAssert();
  return ok;
}

#ifdef ACCEL_USART
static const TagUsartDevice accel_usart_device = {
    .controller = &tagUsart2SyncController,
    .config = &tagUsart2SyncDefaultConfig,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .tx = LINE_ACCEL_TX,
    .rx = LINE_ACCEL_RX,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_CUSTOM,
};

static const TagStUsartRegisterBus accel_register_usart = {
    .device = &accel_usart_device,
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

  tagUsart2SyncEnable(accel_usart_device.config);
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
  const TagUsartDevice *device = (const TagUsartDevice *)context;
  uint8_t buffer[] = {reg, val};

  tagUsartBusWrite(device, buffer, sizeof(buffer));
}

static const TagLis2du12Device compass_tag_accel = {
    .registers = &accel_registers,
    .bus_begin = accelOn,
    .bus_end = accelOff,
    .write_register_byte = accelWriteRegisterByte,
    .write_register_byte_context = &accel_usart_device,
};

const TagLis2du12Device *tagLis2du12Device(void)
{
  return &compass_tag_accel;
}

#endif
