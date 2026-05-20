#include "rtc_device.h"

#if defined(TAG_RTC_RV3028)
#include "power.h"
#include "rv3028.h"

#define RTC_TIMEOUT 100

/*
 * Default RV3028 device binding.
 *
 * The RTC driver itself is parameterized by TagRtcDevice. Most tags use the
 * standard RV3028 wiring on I2CD1, so the common module provides a weak
 * descriptor here. A tag with unusual RTC wiring can override tagRtcDevice()
 * locally without carrying a private copy of the RV3028 register driver.
 */
static const TagI2cDevice rtc_i2c = {
    .controller = &tagI2c1DefaultController,
    .address = RV3028_ADR,
    .timeout = RTC_TIMEOUT,
    .sleep_policy = TAG_I2C_SLEEP_CUSTOM,
};

static const TagRtcDevice rtc_device = {
    .registers = &rtc_i2c,
    .device_on = rtcOn,
    .device_off = rtcOff,
};

const TagRtcDevice *__attribute__((weak)) tagRtcDevice(void)
{
  return &rtc_device;
}
#endif

void tagRtcDeviceBegin(const TagRtcDevice *device)
{
  if (device->device_on)
    device->device_on();
}

void tagRtcDeviceEnd(const TagRtcDevice *device)
{
  if (device->device_off)
    device->device_off();
}

int tagRtcReadRegister(const TagRtcDevice *device, uint8_t reg, uint8_t *buf,
                       uint32_t len)
{
  return tagI2cReadRegister(device->registers, reg, buf, len);
}

int tagRtcWriteRegister(const TagRtcDevice *device, uint8_t reg,
                        const uint8_t *buf, uint32_t len)
{
  return tagI2cWriteRegister(device->registers, reg, buf, len);
}
