#include "rtc_device.h"

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
