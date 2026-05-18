#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "ak09940a.h"

/*
 * Default AK09940A binding for the legacy tag firmware API.
 *
 * The driver in ak09940a.c is parameterized by TagMagDevice and only knows
 * about AK09940A register sequences. This shim is the one place that maps the
 * historical `mag*` and `ak09940_*` entry points onto a default CompassTag
 * SPI/register descriptor, power callbacks, trigger line, and sleep helper.
 */

#if defined(LINE_MAG_CS)
#define AK09940A_DEFAULT_CS LINE_MAG_CS
#elif defined(LINE_AK_CS)
#define AK09940A_DEFAULT_CS LINE_AK_CS
#else
#error "AK09940A default shim needs LINE_MAG_CS or LINE_AK_CS"
#endif

#if defined(LINE_MAG_TRG)
#define AK09940A_DEFAULT_TRG LINE_MAG_TRG
#define AK09940A_HAS_DEFAULT_TRG 1
#elif defined(LINE_AK_TRG)
#define AK09940A_DEFAULT_TRG LINE_AK_TRG
#define AK09940A_HAS_DEFAULT_TRG 1
#endif

static const TagSpiBus ak09940a_spi_bus = {
  .spi = SPI1,
  .cs = AK09940A_DEFAULT_CS,
  .dummy = 0xff,
};

static const TagStSpiRegisterBus ak09940a_spi = {
  .bus = &ak09940a_spi_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};

static const TagRegisterBus ak09940a_registers = {
  .read_register = tagStSpiReadRegister,
  .write_register = tagStSpiWriteRegister,
  .context = &ak09940a_spi,
};

static void ak09940a_default_sleep(int ms)
{
  stopMilliseconds(false, ms);
}

#ifdef AK09940A_HAS_DEFAULT_TRG
static void ak09940a_default_trigger_mode(bool output)
{
  if (output)
    toOutput(AK09940A_DEFAULT_TRG);
  else
    toInput(AK09940A_DEFAULT_TRG);
}

static void ak09940a_default_trigger(void)
{
  palSetLine(AK09940A_DEFAULT_TRG);
  palClearLine(AK09940A_DEFAULT_TRG);
}

static bool ak09940a_default_data_ready_line(void)
{
  return palReadLine(AK09940A_DEFAULT_TRG) == PAL_HIGH;
}
#endif

void __attribute__((weak)) magPowerOn(void) {}

void __attribute__((weak)) magPowerOff(void) {}

void __attribute__((weak)) magBusBegin(void)
{
  magOn();
}

void __attribute__((weak)) magBusEnd(void)
{
  magOff();
}

static const TagMagDevice ak09940a_default_device = {
  .registers = &ak09940a_registers,
  .power_on = magPowerOn,
  .power_off = magPowerOff,
  .bus_begin = magBusBegin,
  .bus_end = magBusEnd,
  .sleep_ms = ak09940a_default_sleep,
#ifdef AK09940A_HAS_DEFAULT_TRG
  .set_trigger_output = ak09940a_default_trigger_mode,
  .trigger = ak09940a_default_trigger,
  .data_ready_line = ak09940a_default_data_ready_line,
#else
  .set_trigger_output = 0,
  .trigger = 0,
  .data_ready_line = 0,
#endif
};

bool magSample(bool single, uint8_t *xyz)
{
  return ak09940aSample(&ak09940a_default_device, single, xyz);
}

void magInit(ak09940_mode_t mode)
{
  ak09940aInit(&ak09940a_default_device, mode);
}

bool magTest(void)
{
  return ak09940aTest(&ak09940a_default_device);
}

bool ak09940_check_whoami(void)
{
  return ak09940aCheckWhoami(&ak09940a_default_device);
}

msg_t ak09940_init_power_down(void)
{
  return ak09940aInitPowerDown(&ak09940a_default_device);
}

msg_t ak09940_init_continuous(ak09940_rate_t rate, ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode)
{
  return ak09940aInitContinuous(&ak09940a_default_device, rate, drive,
                                temp_mode);
}

msg_t ak09940_init_triggered(ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode)
{
  return ak09940aInitTriggered(&ak09940a_default_device, drive, temp_mode);
}

msg_t ak09940_trigger(void)
{
  return ak09940aTrigger(&ak09940a_default_device);
}

bool ak09940_data_ready(bool is_continuous)
{
  return ak09940aDataReady(&ak09940a_default_device, is_continuous);
}

bool ak09940_read_sample(int32_t *mx_raw, int32_t *my_raw, int32_t *mz_raw,
                         int16_t *temp_raw)
{
  return ak09940aReadSample(&ak09940a_default_device, mx_raw, my_raw, mz_raw,
                            temp_raw);
}

bool ak09940_self_test(void)
{
  return ak09940aSelfTest(&ak09940a_default_device);
}
