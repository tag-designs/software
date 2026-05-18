#include "power.h"
#include "rtc_api.h"

#define RTC_TIMEOUT 100

/*
 * Default RV3028 binding for the historical RTC API.
 *
 * The RV3028 driver is parameterized by TagRtcDevice. This shim provides the
 * active tag's default I2C register bus plus the legacy initRTC(),
 * getRTCDateTime(), setRTCDateTime(), and rv3028_GetReg() entry points.
 */

static const TagI2cRegisterBus rtc_i2c = {
    .driver = &I2CD1,
    .address = RV3028_ADR,
    .timeout = RTC_TIMEOUT,
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

int rv3028_GetReg(enum RV3028Reg reg, uint8_t *val, int num)
{
    return rv3028GetReg(tagRtcDevice(), reg, val, num);
}

bool initRTC(void)
{
    return rv3028Init(tagRtcDevice());
}

msg_t setRTCDateTime(RTCDateTime *tm)
{
    return rv3028SetDateTime(tagRtcDevice(), tm);
}

msg_t getRTCDateTime(RTCDateTime *tm)
{
    return rv3028GetDateTime(tagRtcDevice(), tm);
}
