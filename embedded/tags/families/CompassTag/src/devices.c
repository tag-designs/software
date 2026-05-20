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
 * CompassTag-family device bindings.
 *
 * This file describes the concrete board wiring used by the CompassTag family:
 * AK09940A magnetometer on SPI1, LIS2DU12 accelerometer on synchronous USART2,
 * and external flash on SPI1. Common drivers consume these descriptors rather
 * than reaching through generic board globals.
 */

#if defined(LINE_MAG_PWR)
#define AK09940A_PWR LINE_MAG_PWR
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_FLOAT
#else
#define AK09940A_PWR TAG_NO_LINE
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_SAFE_IDLE
#endif

/*
 * Helper declarations.
 *
 * The descriptor table below is intentionally near the top of the file, so the
 * small tag-specific helper functions are declared here and defined below.
 */
static void compassTagMagSleepMilliseconds(int ms);
#if defined(LINE_MAG_TRG)
static void compassTagMagTriggerMode(bool output);
static void compassTagMagTrigger(void);
static bool compassTagMagDataReadyLine(void);
#endif
/*
 * Device descriptors.
 *
 * These structures are the single source of truth for family device wiring and
 * bus/register access. Keep board-line names here rather than spreading them
 * into drivers or application code.
 */
static const TagSpiDevice ak09940a_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_MAG_CS,
    .sck = LINE_MAG_SCK,
    .miso = LINE_MAG_MISO,
    .mosi = LINE_MAG_MOSI,
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

const TagMagDevice tagCompassTagMagDevice = {
    .registers = &ak09940a_registers,
    .power_on = 0,
    .power_off = 0,
    .bus_begin = 0,
    .bus_end = 0,
    .sleep_ms = compassTagMagSleepMilliseconds,
#if defined(LINE_MAG_TRG)
    .set_trigger_output = compassTagMagTriggerMode,
    .trigger = compassTagMagTrigger,
    .data_ready_line = compassTagMagDataReadyLine,
#else
    .set_trigger_output = 0,
    .trigger = 0,
    .data_ready_line = 0,
#endif
};

static const TagUsartDevice accel_usart_device = {
    .controller = &tagUsart2SyncController,
    .config = &tagUsart2SyncDefaultConfig,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .tx = LINE_ACCEL_TX,
    .rx = LINE_ACCEL_RX,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_SAFE_IDLE,
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

const TagLis2du12Device tagCompassTagAccelDevice = {
    .registers = &accel_registers,
    .bus_begin = 0,
    .bus_end = 0,
};

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

/*
 * Helper bindings.
 *
 * These functions adapt board-specific control lines and legacy self-test
 * entry points to the descriptors above.
 */
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
static void compassTagMagTriggerMode(bool output)
{
  if (output)
    toOutput(LINE_MAG_TRG);
  else
    toInput(LINE_MAG_TRG);
}

static void compassTagMagTrigger(void)
{
  palSetLine(LINE_MAG_TRG);
  palClearLine(LINE_MAG_TRG);
}

static bool compassTagMagDataReadyLine(void)
{
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
}
#endif

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

const TagMagDevice *tagAk09940aDevice(void)
{
  return &tagCompassTagMagDevice;
}

const TagLis2du12Device *tagLis2du12Device(void)
{
  return &tagCompassTagAccelDevice;
}

/*
 * Required standby hooks.
 *
 * The common power path calls these while entering low-power states. Device
 * preparation (storage sleep, reset assertion) is separate from MCU standby
 * pin-pull policy.
 */
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
  tagUsartDevicePrepareSleep(&accel_usart_device);
  tagStorageApplyStandbyPins(&tagExternalFlash);
}
