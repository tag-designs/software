#include "hal.h"
#include "app.h"

#define RTC_TIMEOUT 100

static inline uint8_t bcd2bin(uint8_t val)
{
    return ((val >> 4) & 0xf) * 10 + (val & 0xf);
}

static inline uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static int I2C1_MemWrite(uint8_t device, uint8_t reg, unsigned char *buffer,
                         uint16_t size, uint32_t timeout)
{
    uint8_t txbuf[10];
    txbuf[0] = reg;
    for (int i = 0; i < 8 && i < size; i++)
    {
        txbuf[i + 1] = buffer[i];
    }
    return i2cMasterTransmitTimeout(&I2CD1, device, txbuf, size + 1, 0, 0,
                                    timeout);
}

int rv8803_GetReg(enum RV8803Reg reg, uint8_t *val, int num)
{
    return i2cMasterTransmitTimeout(&I2CD1, RV8803_ADR, &reg, 1, val, num,
                                    RTC_TIMEOUT);
}

static int rv8803_SetReg(enum RV8803Reg reg, unsigned char *val, int num)
{
    return I2C1_MemWrite(RV8803_ADR, reg, val, num, RTC_TIMEOUT);
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
