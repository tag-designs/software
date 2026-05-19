#include "hal.h"

#include "custom.h"
#include "gpio_utils.h"
#include "lis2du12.h"
#include "power.h"

/*
 * CompassTag-family device bindings that are not yet general tag drivers.
 *
 * The LIS2DU12 code in this family is tailored to the CompassTag USART wiring
 * and sampling needs, so keep its TagLis2du12Device descriptor beside the
 * family code instead of burying it in the shared power file.
 */

#ifdef ACCEL_USART
static void usartEnable(void)
{
  rccEnableUSART2(true);
  rccResetUSART2();

  USART2->BRR = 0x10;
  USART2->CR2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL;
  USART2->CR3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT;
  USART2->CR1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
  tagMarkUsart2On();
}

static void usartDisable(void)
{
  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
  tagMarkUsart2Off();
}

static const TagUsartBus accel_usart_bus = {
    .usart = USART2,
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

  usartEnable();
}

void accelOff(void)
{
#ifdef COMPASS_TAG
  palSetLine(LINE_ACCEL_CS);
  usartDisable();
  toOutput(LINE_ACCEL_SCK);
  toOutput(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
#else
  usartDisable();
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
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagEnableStandbyPulldown(LINE_ACCEL_TX);
  tagEnableStandbyPulldown(LINE_ACCEL_SCK);
}
#endif
