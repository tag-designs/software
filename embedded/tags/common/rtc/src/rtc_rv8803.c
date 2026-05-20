#include "hal.h"
#include "power.h"
#include "rtc_api.h"
#include "sensor_io.h"

#define RTC_TIMEOUT 100

static inline uint8_t bcd2bin(uint8_t val)
{
    return ((val >> 4) & 0xf) * 10 + (val & 0xf);
}

static inline uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static const TagI2cDevice rv8803_i2c = {
    .controller = &tagI2c1DefaultController,
    .address = RV8803_ADR,
    .timeout = RTC_TIMEOUT,
    .sleep_policy = TAG_I2C_SLEEP_CUSTOM,
};

int rv8803_GetReg(enum RV8803Reg reg, uint8_t *val, int num)
{
    return tagI2cReadRegister(&rv8803_i2c, reg, val, num);
}

static int rv8803_SetReg(enum RV8803Reg reg, unsigned char *val, int num)
{
    return tagI2cWriteRegister(&rv8803_i2c, reg, val, num);
}

bool initRTC(void)
{
    bool result = true;
    const unsigned char bufval = 4;
    unsigned char buf = bufval; // 2<<2; // 1024hz clock input
    rtcOn();
    rv8803_SetReg(RV8803_EXT, &buf, 1);
    rv8803_GetReg(RV8803_EXT, &buf, 1);
    rtcOff();
    result = (buf == bufval); 
    if (RTCD1.rtc->PRER != STM32_RTC_PRER_BITS)
    {
        RTCD1.rtc->WPR = 0xCA;
        RTCD1.rtc->WPR = 0x53;
        RTCD1.rtc->ISR |= RTC_ISR_INIT;
        while ((RTCD1.rtc->ISR & RTC_ISR_INITF) == 0)
            ;
        RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
        RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
        RTCD1.rtc->ISR &= ~RTC_ISR_INIT;
    }
    return result && (RTCD1.rtc->PRER == STM32_RTC_PRER_BITS);
}

msg_t setRTCDateTime(RTCDateTime *tm)
{
    int ret = MSG_OK;

    uint8_t date[7];

    rtcOn();
    do
    {
        uint32_t seconds = tm->millisecond / 1000;
        date[RV8803_SEC] = bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date[RV8803_MIN] = bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date[RV8803_HOUR] = bin2bcd(seconds % 24);
        date[RV8803_WDAY] = 1 << (tm->dayofweek - 1);
        date[RV8803_DAY] = bin2bcd(tm->day);
        date[RV8803_MONTH] = bin2bcd(tm->month);
        date[RV8803_YEAR] = bin2bcd(tm->year);
        ret = rv8803_SetReg(RV8803_SEC, date, sizeof(date));

        if (ret != MSG_OK)
            break;

        uint8_t status = 0;

        ret = rv8803_SetReg(RV8803_FLAG, &status, 1);

    } while (0);
    rtcOff();

    return ret;
}

msg_t getRTCDateTime(RTCDateTime *tm)
{
    int ret = MSG_OK;

    uint8_t status = 0;
    uint8_t date[7];
    rtcOn();
    do
    {
        ret = rv8803_GetReg(RV8803_FLAG, &status, 1);
        if ((ret != MSG_OK) || (status & RV8803_FLAG_V2F))
        {
            ret = (ret == MSG_OK) ? -1 : ret;
            break;
        }
        ret = rv8803_GetReg(RV8803_SEC, date, sizeof(date));

        if (ret != MSG_OK)
        {
            break;
        }
        tm->millisecond = bcd2bin(date[RV8803_SEC] & 0x7f);
        tm->millisecond += 60 * bcd2bin(date[RV8803_MIN] & 0x7f);
        tm->millisecond += 3600 * bcd2bin(date[RV8803_HOUR] & 0x3f);
        tm->millisecond *= 1000;
        tm->dayofweek = bcd2bin(date[RV8803_WDAY] & 0x7) + 1;
        tm->day = bcd2bin(date[RV8803_DAY] & 0x3f);
        tm->month = bcd2bin(date[RV8803_MONTH] & 0x3f);
        tm->year = bcd2bin(date[RV8803_YEAR]);
    } while (0);
    rtcOff();
    

    return ret;
}
