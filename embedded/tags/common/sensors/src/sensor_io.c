#include "sensor_io.h"

#define TAG_I2C_REGISTER_MAX_WRITE 16

int tagRegisterWrite(const TagRegisterDevice *device, uint8_t reg,
                     const uint8_t *buf, uint32_t len)
{
  const void *context = device->context ? device->context : device;
  return device->write_register(context, reg, buf, len);
}

int tagRegisterRead(const TagRegisterDevice *device, uint8_t reg, uint8_t *buf,
                    uint32_t len)
{
  const void *context = device->context ? device->context : device;
  return device->read_register(context, reg, buf, len);
}

void tagRegisterBusBegin(const TagRegisterDevice *device)
{
  tagBusBegin(device->bus);
}

void tagRegisterBusEnd(const TagRegisterDevice *device)
{
  tagBusEnd(device->bus);
}

static inline I2CDriver *tagI2cDeviceDriver(const TagI2cDevice *device)
{
  return device->controller->driver;
}

int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len)
{
  const TagI2cDevice *device = (const TagI2cDevice *)io;
  uint8_t txbuf[TAG_I2C_REGISTER_MAX_WRITE + 1];

  if (len > TAG_I2C_REGISTER_MAX_WRITE) {
    return MSG_RESET;
  }

  txbuf[0] = reg;
  for (uint32_t i = 0; i < len; i++) {
    txbuf[i + 1] = buf[i];
  }

  return i2cMasterTransmitTimeout(tagI2cDeviceDriver(device), device->address,
                                  txbuf, len + 1, 0, 0, device->timeout);
}

int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len)
{
  const TagI2cDevice *device = (const TagI2cDevice *)io;

  return i2cMasterTransmitTimeout(tagI2cDeviceDriver(device), device->address,
                                  &reg, 1, buf, len, device->timeout);
}

int tagStSpiWriteRegisterDevice(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len)
{
  const TagRegisterDevice *registers = (const TagRegisterDevice *)io;
  const TagSpiDevice *device = (const TagSpiDevice *)registers->bus->device;
  uint8_t command = (uint8_t)((reg & (uint8_t)~registers->read_mask) |
                             registers->write_mask);

  tagSpiSelect(device);
  tagSpiWrite(device, &command, 1);
  tagSpiWrite(device, buf, len);
  tagSpiDeselect(device);

  return 0;
}

int tagStSpiReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len)
{
  const TagRegisterDevice *registers = (const TagRegisterDevice *)io;
  const TagSpiDevice *device = (const TagSpiDevice *)registers->bus->device;
  uint8_t command = (uint8_t)(reg | registers->read_mask);

  tagSpiSelect(device);
  tagSpiWrite(device, &command, 1);
  tagSpiRead(device, buf, len);
  tagSpiDeselect(device);

  return 0;
}

int tagStUsartWriteRegisterDevice(const void *io, uint8_t reg,
                                  const uint8_t *buf, uint32_t len)
{
  const TagRegisterDevice *registers = (const TagRegisterDevice *)io;
  const TagUsartDevice *device =
      (const TagUsartDevice *)registers->bus->device;
  uint8_t command = (uint8_t)((reg & (uint8_t)~registers->read_mask) |
                             registers->write_mask);

  tagUsartSelect(device);
  tagUsartWrite(device, &command, 1);
  tagUsartWrite(device, buf, len);
  tagUsartDeselect(device);

  return 0;
}

int tagStUsartReadRegisterDevice(const void *io, uint8_t reg, uint8_t *buf,
                                 uint32_t len)
{
  const TagRegisterDevice *registers = (const TagRegisterDevice *)io;
  const TagUsartDevice *device =
      (const TagUsartDevice *)registers->bus->device;
  uint8_t command = (uint8_t)(reg | registers->read_mask);

  tagUsartSelect(device);
  tagUsartWrite(device, &command, 1);
  tagUsartRead(device, buf, len);
  tagUsartDeselect(device);

  return 0;
}
