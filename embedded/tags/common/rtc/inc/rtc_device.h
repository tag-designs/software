#ifndef TAG_RTC_DEVICE_H
#define TAG_RTC_DEVICE_H

#include "sensor_io.h"

#include <stdint.h>

/*
 * RTC device descriptor.
 *
 * The RV3028/RV3032/RV8803 drivers own chip register sequences. This
 * descriptor supplies the tag-specific I2C register bus and the power/bus
 * callbacks needed before and after each RTC transaction.
 */
typedef void (*TagRtcPower)(void);

typedef struct {
  const TagI2cDevice *registers;
  TagRtcPower device_on;
  TagRtcPower device_off;
} TagRtcDevice;

const TagRtcDevice *tagRtcDevice(void);

void tagRtcDeviceBegin(const TagRtcDevice *device);
void tagRtcDeviceEnd(const TagRtcDevice *device);
int tagRtcReadRegister(const TagRtcDevice *device, uint8_t reg, uint8_t *buf,
                       uint32_t len);
int tagRtcWriteRegister(const TagRtcDevice *device, uint8_t reg,
                        const uint8_t *buf, uint32_t len);

#endif
