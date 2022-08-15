#include "hal.h"
#include "app.h"
#include "rv3032.h"

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

static int rv3032_GetReg(enum RV3032Reg reg, uint8_t *val, int num)
{
    return i2cMasterTransmitTimeout(&I2CD1, RV3032_ADR, &reg, 1, val, num,
                                    RTC_TIMEOUT);
}

static int rv3032_SetReg(enum RV3032Reg reg, unsigned char *val, int num)
{
    return I2C1_MemWrite(RV3032_ADR, reg, val, num, RTC_TIMEOUT);
}

static int rv3032_EEPROM_Exec(unsigned char addr, unsigned char *val, unsigned char cmd)
{
    int i;
    unsigned char tmp;

    for (i = 0; i < 10; i++)
    { 
        chThdSleepMilliseconds(10);
        rv3032_GetReg(RV3032_TEMP_LSB, &tmp, 1);
        if (!(tmp & RV3032_TEMP_LSB_EEBUSY))
            break;
        
    }
    if (i == 10)
    {
        return MSG_TIMEOUT;
    }

    if ((cmd == RV3032_EEPROM_CMD_WRITE) ||
        (cmd == RV3032_EEPROM_CMD_READ))
        rv3032_SetReg(RV3032_EEPROM_ADDR, &addr, 1);
    if (cmd == RV3032_EEPROM_CMD_WRITE)
    {
        rv3032_SetReg(RV3032_EEPROM_DATA, val, 1);
    }
    tmp = 0;
    rv3032_SetReg(RV3032_EEPROM_CMD, &tmp, 1);
    rv3032_SetReg(RV3032_EEPROM_CMD, &cmd, 1);

    for (i = 0; i < 10; i++)
    {
        chThdSleepMilliseconds(10);
        rv3032_GetReg(RV3032_TEMP_LSB, &tmp, 1);
        if (!(tmp & RV3032_TEMP_LSB_EEBUSY))
            break;
    }

    if (i == 10)
    {
        return MSG_TIMEOUT;
    }
    else
    {
        if (cmd == RV3032_EEPROM_CMD_READ)
        {
            rv3032_GetReg(RV3032_EEPROM_DATA, val, 1);
        }
        return MSG_OK;
    }
}

bool initRTC(void)
{
    unsigned char tmp;
    unsigned char clkout;
    unsigned char ctrl1;

    bool result = false;
    rtcOn();
    do
    {
        int i;
        clkout = 0;
        if (rv3032_GetReg(RV3032_CLKOUT2, &clkout, 1) != MSG_OK)
            break;
        rv3032_EEPROM_Exec(RV3032_CLKOUT2, &tmp, RV3032_EEPROM_CMD_READ);
        if (tmp == (RV3032_CLKOUT_VAL))
        {
            result = true;
            break;
        }

        // Write to EEPROM mirror

        rv3032_GetReg(RV3032_CTRL1, &ctrl1, 1);

        // disable automatic refresh

        ctrl1 |= RV3032_CTRL1_EERD;
        rv3032_SetReg(RV3032_CTRL1, &ctrl1, 1);
        for (i = 0; i < 10; i++)
        {
            chThdSleepMilliseconds(10);
            rv3032_GetReg(RV3032_TEMP_LSB, &tmp, 1);
            if (!(tmp & RV3032_TEMP_LSB_EEBUSY))
                break;
        }
        if (i == 10)
            break;

        clkout = RV3032_CLKOUT_VAL;
        if (rv3032_EEPROM_Exec(RV3032_CLKOUT2, &clkout, RV3032_EEPROM_CMD_WRITE))
            break;

        if (rv3032_EEPROM_Exec(RV3032_CLKOUT2, &clkout, RV3032_EEPROM_CMD_REFRESH))
            break;

        // reenable EEPROM->Ram refresh

        ctrl1 &= ~RV3032_CTRL1_EERD;
        rv3032_SetReg(RV3032_CTRL1, &ctrl1, 1);
        rv3032_EEPROM_Exec(RV3032_CLKOUT2, &tmp, RV3032_EEPROM_CMD_READ);

        if (tmp == RV3032_CLKOUT_VAL)
        {
            result = true;
        }
    } while (0);
    rtcOff();

    // Check RTC dividers !

    uint32_t prer = RTCD1.rtc->PRER;
    if (prer != STM32_RTC_PRER_BITS)
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
    return result;
}

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t wday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} rv3032_timedate;

msg_t setRTCDateTime(RTCDateTime *tm)
{
    int ret = MSG_OK;
    rv3032_timedate date;

    rtcOn();
    do
    {
        uint32_t seconds = tm->millisecond / 1000;
        date.seconds = bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date.minutes= bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date.hours = bin2bcd(seconds % 24);
        date.wday = 1 << (tm->dayofweek - 1);
        date.day = bin2bcd(tm->day);
        date.month = bin2bcd(tm->month);
        date.year = bin2bcd(tm->year);

        ret = rv3032_SetReg(RV3032_SEC, (uint8_t *) &date, sizeof(date));

        if (ret != MSG_OK)
            break;

        uint8_t status = 0;
        ret = rv3032_SetReg(RV3032_STATUS, &status, 1);
    } while (0);
    rtcOff();
    return ret;
}

msg_t getRTCDateTime(RTCDateTime *tm)
{
    int ret = MSG_OK;

    uint8_t status = 0;
    rv3032_timedate date;
    rtcOn();
    do
    {
        ret = rv3032_GetReg(RV3032_STATUS, &status, 1);
        if ((ret != MSG_OK) || (status & RV3032_STATUS_PORF))
        {
            ret = (ret == MSG_OK) ? -1 : ret;
            break;
        }
        ret = rv3032_GetReg(RV3032_SEC, (uint8_t *) &date, sizeof(date));
        if (ret != MSG_OK)
        {
            break;
        }
        tm->millisecond = bcd2bin(date.seconds & 0x7f);
        tm->millisecond += 60 * bcd2bin(date.minutes & 0x7f);
        tm->millisecond += 3600 * bcd2bin(date.hours & 0x3f);
        tm->millisecond *= 1000;
        tm->dayofweek = bcd2bin(date.wday & 0x7) + 1;
        tm->day = bcd2bin(date.day & 0x3f);
        tm->month = bcd2bin(date.month & 0x3f);
        tm->year = bcd2bin(date.year);
    } while (0);
    rtcOff();
    return ret;
}
