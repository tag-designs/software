/**
 * @file rtc_rv3028.c
 * @brief RV3028 external RTC register driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "debug_log.h"
#include "rtc_api.h"

#ifndef TAG_RTC_STM32U3_COMPAT
#define TAG_RTC_STM32U3_COMPAT 0
#endif

#ifndef TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT
#define TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT 0
#endif

#ifndef RV3028_CLKOUT_VAL
#error "Unsupported STM32 RTC reference frequency for RV3028 CLKOUT selection"
#endif

#if TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT && (RV3028_CLKOUT_VAL != 0)
#error "This RV3028 build requires direct 32.768 kHz CLKOUT"
#endif

#ifndef IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION
#define IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION 0
#endif

#define RV3028_EEOFFSET_PPM_PER_STEP (1000000.0f / (16384.0f * 64.0f))
#define RV3028_EEOFFSET_SIGN_BIT 0x100u
#define RV3028_EEOFFSET_RANGE 0x200

static int16_t rv3028_eeoffset_steps = 0;
static float rv3028_clock_error_ppm = 0.0f;
static bool rv3028_clock_correction_valid = false;

/** @name BCD conversion helpers
 * The RV3028 stores calendar fields as packed BCD while ChibiOS RTCDateTime
 * uses binary fields.
 * @{
 */
/**
 * @brief Convert one packed BCD byte to binary.
 *
 * @param[in] val Packed BCD value.
 * @return Binary value.
 */
static inline uint8_t bcd2bin(uint8_t val)
{
    return ((val >> 4) & 0xf) * 10 + (val & 0xf);
}

/**
 * @brief Convert one binary byte to packed BCD.
 *
 * @param[in] val Binary value.
 * @return Packed BCD value.
 */
static inline uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}
/** @} */

#if TAG_RTC_STM32U3_COMPAT
static inline void tagRtcEnterInitMode(void)
{
#if defined(RCC_APB1ENR1_RTCAPBEN)
    RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN;
#endif
#if defined(PWR_DBPR_DBP)
    PWR->DBPR |= PWR_DBPR_DBP;
#endif
    RTCD1.rtc->WPR = 0xCA;
    RTCD1.rtc->WPR = 0x53;
#if defined(RTC_ISR_INIT)
    RTCD1.rtc->ISR |= RTC_ISR_INIT;
#elif defined(RTC_ICSR_INIT)
    RTCD1.rtc->ICSR |= RTC_ICSR_INIT;
#endif
}

static inline bool tagRtcInitModeReady(void)
{
#if defined(RTC_ISR_INITF)
    return (RTCD1.rtc->ISR & RTC_ISR_INITF) != 0;
#elif defined(RTC_ICSR_INITF)
    return (RTCD1.rtc->ICSR & RTC_ICSR_INITF) != 0;
#else
    return true;
#endif
}

static inline void tagRtcLeaveInitMode(void)
{
#if defined(RTC_ISR_INIT)
    RTCD1.rtc->ISR &= ~RTC_ISR_INIT;
#elif defined(RTC_ICSR_INIT)
    RTCD1.rtc->ICSR &= ~RTC_ICSR_INIT;
#endif
}

static bool tagRtcWaitForInitMode(void)
{
    for (uint32_t timeout = 10000; timeout > 0; timeout--)
    {
        if (tagRtcInitModeReady())
        {
            return true;
        }
    }
    return false;
}

static bool tagRtcRecalibrationReady(void)
{
#if defined(RTC_ICSR_RECALPF)
    return (RTCD1.rtc->ICSR & RTC_ICSR_RECALPF) == 0U;
#else
    return true;
#endif
}

static bool tagRtcWaitForRecalibrationReady(void)
{
    for (uint32_t timeout = 10000; timeout > 0; timeout--)
    {
        if (tagRtcRecalibrationReady())
        {
            return true;
        }
    }
    return false;
}

static void rv3028MapOffsetToStm32Calr(int16_t steps, bool *calp,
                                       uint16_t *calm)
{
    if (steps == 0)
    {
        *calp = false;
        *calm = 0;
    }
    else if (steps < 0)
    {
        *calp = false;
        *calm = (uint16_t)(-steps);
    }
    else
    {
        *calp = true;
        *calm = (uint16_t)(512 - steps);
    }
}

static bool rv3028ApplyStm32SmoothCalibration(int16_t steps)
{
    bool calp;
    uint16_t calm;

    rv3028MapOffsetToStm32Calr(steps, &calp, &calm);
    if (calm > (uint16_t)(RTC_CALR_CALM >> RTC_CALR_CALM_Pos))
    {
        return false;
    }
    if (!tagRtcWaitForRecalibrationReady())
    {
        debug_log_printf("STM32 RTC calibration busy timeout\r\n");
        return false;
    }

#if defined(RCC_APB1ENR1_RTCAPBEN)
    RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN;
#endif
#if defined(PWR_DBPR_DBP)
    PWR->DBPR |= PWR_DBPR_DBP;
#endif
    RTCD1.rtc->WPR = 0xCA;
    RTCD1.rtc->WPR = 0x53;
    RTCD1.rtc->CALR = ((uint32_t)calm << RTC_CALR_CALM_Pos) |
                      (calp ? RTC_CALR_CALP : 0U);
    RTCD1.rtc->WPR = 0xFF;
    return true;
}
#endif

static int16_t rv3028DecodeEEOffset(uint8_t offset, uint8_t backup)
{
    uint16_t raw9 = ((uint16_t)offset << 1) |
                    ((backup >> 7) & 0x01u);

    if ((raw9 & RV3028_EEOFFSET_SIGN_BIT) != 0U)
    {
        return (int16_t)((int32_t)raw9 - RV3028_EEOFFSET_RANGE);
    }
    return (int16_t)raw9;
}

/** @name RV3028 register access
 * Register and EEPROM helpers keep the calendar code independent of the I2C
 * transaction wrapper used by the current tag.
 * @{
 */
/**
 * @brief Read one or more RV3028 registers.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] reg First register to read.
 * @param[out] val Destination buffer for register bytes.
 * @param[in] num Number of bytes to read.
 * @return MSG_OK on success or a bus error.
 */
int rv3028GetReg(const TagRtcDevice *device, enum RV3028Reg reg,
                 uint8_t *val, int num)
{
    return tagRtcReadRegister(device, reg, val, num);
}

static bool rv3028ReadClockCorrection(const TagRtcDevice *device,
                                      int16_t *steps, float *ppm)
{
    uint8_t regs[2];

    if (rv3028GetReg(device, RV3028_OFFSET, regs, sizeof(regs)) != MSG_OK)
    {
        return false;
    }

    *steps = rv3028DecodeEEOffset(regs[0], regs[1]);
    *ppm = (float)(*steps) * RV3028_EEOFFSET_PPM_PER_STEP;
    return true;
}

static void rv3028UpdateClockCorrection(const TagRtcDevice *device)
{
    int16_t steps;
    float ppm;

    if (rv3028ReadClockCorrection(device, &steps, &ppm))
    {
        rv3028_eeoffset_steps = steps;
        rv3028_clock_error_ppm = ppm;
        rv3028_clock_correction_valid = true;
    }
    else
    {
        rv3028_eeoffset_steps = 0;
        rv3028_clock_error_ppm = 0.0f;
        rv3028_clock_correction_valid = false;
        debug_log_printf("RV3028 EEOffset read failed\r\n");
    }
}

float rv3028ClockErrorPpm(void)
{
    return rv3028_clock_error_ppm;
}

bool rv3028ClockCorrectionValid(void)
{
    return rv3028_clock_correction_valid;
}

bool rv3028RefreshClockCorrection(const TagRtcDevice *device)
{
    tagRtcDeviceBegin(device);
    rv3028UpdateClockCorrection(device);
    tagRtcDeviceEnd(device);
    return rv3028_clock_correction_valid;
}

bool rv3028ApplyClockCorrection(void)
{
#if TAG_RTC_STM32U3_COMPAT && IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION
    if (!rv3028_clock_correction_valid)
    {
        debug_log_printf("RV3028 EEOffset correction unavailable\r\n");
        return false;
    }
    return rv3028ApplyStm32SmoothCalibration(rv3028_eeoffset_steps);
#else
    return true;
#endif
}

/**
 * @brief Write one or more RV3028 registers.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] reg First register to write.
 * @param[in] val Source buffer for register bytes.
 * @param[in] num Number of bytes to write.
 * @return MSG_OK on success or a bus error.
 */
static int rv3028SetReg(const TagRtcDevice *device, enum RV3028Reg reg,
                        const unsigned char *val, int num)
{
    return tagRtcWriteRegister(device, reg, val, num);
}

/**
 * @brief Execute one RV3028 EEPROM command and wait for completion.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] addr EEPROM address for read/write commands.
 * @param[in,out] val Byte to write or destination for read commands.
 * @param[in] cmd RV3028 EEPROM command code.
 * @return MSG_OK on success or MSG_TIMEOUT if the EEPROM stays busy.
 */
static int rv3028EEPROMExec(const TagRtcDevice *device, unsigned char addr,
                            unsigned char *val, unsigned char cmd)
{
    int i;
    unsigned char tmp;

    for (i = 0; i < 10; i++)
    { 
        chThdSleepMilliseconds(10);
        rv3028GetReg(device, RV3028_STATUS, &tmp, 1);
        if (!(tmp & RV3028_STATUS_EEBUSY))
            break;
        
    }
    if (i == 10)
    {
        return MSG_TIMEOUT;
    }

    if ((cmd == RV3028_EEPROM_CMD_WRITE) ||
        (cmd == RV3028_EEPROM_CMD_READ))
        rv3028SetReg(device, RV3028_EEPROM_ADDR, &addr, 1);
    if (cmd == RV3028_EEPROM_CMD_WRITE)
    {
        rv3028SetReg(device, RV3028_EEPROM_DATA, val, 1);
    }
    tmp = 0;
    rv3028SetReg(device, RV3028_EEPROM_CMD, &tmp, 1);
    rv3028SetReg(device, RV3028_EEPROM_CMD, &cmd, 1);

    for (i = 0; i < 10; i++)
    {
        chThdSleepMilliseconds(10);
        rv3028GetReg(device, RV3028_STATUS, &tmp, 1);
        if (!(tmp & RV3028_STATUS_EEBUSY))
            break;
    }

    if (i == 10)
    {
        return MSG_TIMEOUT;
    }
    else
    {
        if (cmd == RV3028_EEPROM_CMD_READ)
        {
            rv3028GetReg(device, RV3028_EEPROM_DATA, val, 1);
        }
        return MSG_OK;
    }
}
/** @} */

/** @name RV3028 lifecycle and calendar access
 * Public driver operations used by the generic tag RTC shim.
 * @{
 */
/**
 * @brief Initialize the RV3028 oscillator output and STM32 RTC dividers.
 *
 * @param[in] device RTC device descriptor.
 * @return true when the RTC configuration is usable.
 */
bool rv3028Init(const TagRtcDevice *device)
{
#if !TAG_RTC_STM32U3_COMPAT
    unsigned char tmp;
    unsigned char clkout;
    unsigned char ctrl1;

    bool result = false;
    tagRtcDeviceBegin(device);
    rv3028GetReg(device, RV3028_CTRL1, &ctrl1, 1);
    ctrl1 |= RV3028_CTRL1_EERD;
    rv3028SetReg(device, RV3028_CTRL1, &ctrl1, 1);
    rv3028UpdateClockCorrection(device);
    do
    {

        clkout = 0;

        // check if clkout register is already correctly configured

        if (rv3028GetReg(device, RV3028_CLKOUT, &clkout, 1) != MSG_OK)
            break;
        rv3028EEPROMExec(device, RV3028_CLKOUT, &tmp,
                         RV3028_EEPROM_CMD_READ);
        if ((tmp == (0xC0 | (RV3028_CLKOUT_VAL))) &&
            (clkout == (0xC0 | (RV3028_CLKOUT_VAL))))
        {
            result = true;
            break;
        }

       /*
        for (i = 0; i < 10; i++)
        {
            chThdSleepMilliseconds(10);
            rv3028GetReg(device, RV3028_STATUS, &tmp, 1);
            if (!(tmp & RV3028_STATUS_EEBUSY))
                break;
        }
        if (i == 10)
            break;
        */

        //clkout = 0xC0 | (RV3028_CLKOUT_VAL&7);
        //rv3028SetReg(device, RV3028_CLKOUT, &clkout, 1);

        //clkout = 0;
       // if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout, RV3028_EEPROM_CMD_WRITE))
        //    break;

        /* Only CLKOUT is rewritten. RV3028 EEOffset is factory calibration
           data and must remain read-only. */
        clkout = 0xC0 | (RV3028_CLKOUT_VAL & 7);
        if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout,
                             RV3028_EEPROM_CMD_WRITE))
            break;

        if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout,
                             RV3028_EEPROM_CMD_REFRESH))
            break;

        rv3028EEPROMExec(device, RV3028_CLKOUT, &tmp,
                         RV3028_EEPROM_CMD_READ);
        rv3028GetReg(device, RV3028_CLKOUT, &clkout, 1);
        if ((tmp == clkout) && (tmp == (0xC0 | (RV3028_CLKOUT_VAL))))
        {
            result = true;
        }
    } while (0);
     // reenable EEPROM->Ram refresh
    ctrl1 &= ~RV3028_CTRL1_EERD;
    rv3028SetReg(device, RV3028_CTRL1, &ctrl1, 1);
    tagRtcDeviceEnd(device);

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
#else
    unsigned char tmp;
    unsigned char clkout;
    unsigned char ctrl1;

    bool result = false;
    tagRtcDeviceBegin(device);
    rv3028GetReg(device, RV3028_CTRL1, &ctrl1, 1);
    ctrl1 |= RV3028_CTRL1_EERD;
    rv3028SetReg(device, RV3028_CTRL1, &ctrl1, 1);
    rv3028UpdateClockCorrection(device);
    do
    {
     
        clkout = 0;

        // check if clkout register is already correctly configured

        if (rv3028GetReg(device, RV3028_CLKOUT, &clkout, 1) != MSG_OK)
            break;
        rv3028EEPROMExec(device, RV3028_CLKOUT, &tmp,
                         RV3028_EEPROM_CMD_READ);
        if (
#if TAG_RTC_STM32U3_COMPAT
            (tmp == (0xC0 | (RV3028_CLKOUT_VAL))) &&
            (clkout == (0xC0 | (RV3028_CLKOUT_VAL)))
#else
            (tmp == (0xC0 | (RV3028_CLKOUT_VAL))) &&
            (clkout == (0xC0 | (RV3028_CLKOUT_VAL)))
#endif
            )
        {
            result = true;
            break;
        }

       /*
        for (i = 0; i < 10; i++)
        {
            chThdSleepMilliseconds(10);
            rv3028GetReg(device, RV3028_STATUS, &tmp, 1);
            if (!(tmp & RV3028_STATUS_EEBUSY))
                break;
        }
        if (i == 10)
            break;
        */

        //clkout = 0xC0 | (RV3028_CLKOUT_VAL&7);
        //rv3028SetReg(device, RV3028_CLKOUT, &clkout, 1);

        //clkout = 0;
       // if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout, RV3028_EEPROM_CMD_WRITE))
        //    break;

        /* Only CLKOUT is rewritten. RV3028 EEOffset is factory calibration
           data and must remain read-only. */
        clkout = 0xC0 | (RV3028_CLKOUT_VAL & 7);
        if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout,
                             RV3028_EEPROM_CMD_WRITE))
            break;

        if (rv3028EEPROMExec(device, RV3028_CLKOUT, &clkout,
                             RV3028_EEPROM_CMD_REFRESH))
            break;
        
        rv3028EEPROMExec(device, RV3028_CLKOUT, &tmp,
                         RV3028_EEPROM_CMD_READ);
        rv3028GetReg(device, RV3028_CLKOUT, &clkout, 1);
        if ((tmp == clkout) &&
#if TAG_RTC_STM32U3_COMPAT
            (tmp == (0xC0 | (RV3028_CLKOUT_VAL))))
#else
            (tmp == (0xC0 | (RV3028_CLKOUT_VAL))))
#endif
        {
            result = true;
        }
    } while (0);
     // reenable EEPROM->Ram refresh
    ctrl1 &= ~RV3028_CTRL1_EERD;
    rv3028SetReg(device, RV3028_CTRL1, &ctrl1, 1);
    tagRtcDeviceEnd(device);

    // Check RTC dividers !

    uint32_t prer = RTCD1.rtc->PRER;
    if (prer != STM32_RTC_PRER_BITS)
    {
#if TAG_RTC_STM32U3_COMPAT
        RTCD1.rtc->WPR = 0xCA;
        RTCD1.rtc->WPR = 0x53;
        tagRtcEnterInitMode();
        if (tagRtcWaitForInitMode())
        {
            RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
            RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
        }
        else
        {
            result = false;
            debug_log_printf("STM32 RTC init mode timeout\r\n");
        }
        tagRtcLeaveInitMode();
#else
        RTCD1.rtc->WPR = 0xCA;
        RTCD1.rtc->WPR = 0x53;
        RTCD1.rtc->ISR |= RTC_ISR_INIT;
        while ((RTCD1.rtc->ISR & RTC_ISR_INITF) == 0)
            ;
        RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
        RTCD1.rtc->PRER = STM32_RTC_PRER_BITS;
        RTCD1.rtc->ISR &= ~RTC_ISR_INIT;
#endif
    }
    return result;
#endif
}

/**
 * @brief Write date/time to the RV3028.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] tm RTC date/time structure to write.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t rv3028SetDateTime(const TagRtcDevice *device, RTCDateTime *tm)
{
    int ret = MSG_OK;

    uint8_t date[7];

    tagRtcDeviceBegin(device);
    do
    {
        uint32_t seconds = tm->millisecond / 1000;
        date[RV3028_SEC] = bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date[RV3028_MIN] = bin2bcd(seconds % 60);
        seconds = seconds / 60;
        date[RV3028_HOUR] = bin2bcd(seconds % 24);
        date[RV3028_WDAY] = 1 << (tm->dayofweek - 1);
        date[RV3028_DAY] = bin2bcd(tm->day);
        date[RV3028_MONTH] = bin2bcd(tm->month);
        date[RV3028_YEAR] = bin2bcd(tm->year);
        ret = rv3028SetReg(device, RV3028_SEC, date, sizeof(date));
        if (ret != MSG_OK)
            break;

        uint8_t status = 0;
        ret = rv3028SetReg(device, RV3028_STATUS, &status, 1);
    } while (0);


    tagRtcDeviceEnd(device);

    if (ret != MSG_OK){
        debug_log_printf("Error in rv3028 setRTCDateTime\r\n");

    }
    return ret;
}

/**
 * @brief Read date/time from the RV3028.
 *
 * @param[in] device RTC device descriptor.
 * @param[out] tm RTC date/time structure to populate.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t rv3028GetDateTime(const TagRtcDevice *device, RTCDateTime *tm)
{
    int ret = MSG_OK;

    uint8_t status = 0;
    uint8_t date[7];
    tagRtcDeviceBegin(device);
    do
    {
        ret = rv3028GetReg(device, RV3028_STATUS, &status, 1);
        if ((ret != MSG_OK) || (status & RV3028_STATUS_PORF))
        {
#if TAG_RTC_STM32U3_COMPAT
            if (ret == MSG_OK)
            {
                debug_log_printf("rv3028 PORF set status=0x%02x\r\n", status);
            }
            else
            {
                debug_log_printf("rv3028 status read failed ret=%d\r\n", ret);
            }
#endif
            ret = (ret == MSG_OK) ? -1 : ret;
            break;
        }
        ret = rv3028GetReg(device, RV3028_SEC, date, sizeof(date));
        if (ret != MSG_OK)
        {
            break;
        }
        tm->millisecond = bcd2bin(date[RV3028_SEC] & 0x7f);
        tm->millisecond += 60 * bcd2bin(date[RV3028_MIN] & 0x7f);
        tm->millisecond += 3600 * bcd2bin(date[RV3028_HOUR] & 0x3f);
        tm->millisecond *= 1000;
        tm->dayofweek = bcd2bin(date[RV3028_WDAY] & 0x7) + 1;
        tm->day = bcd2bin(date[RV3028_DAY] & 0x3f);
        tm->month = bcd2bin(date[RV3028_MONTH] & 0x3f);
        tm->year = bcd2bin(date[RV3028_YEAR]);
    } while (0);

  
    tagRtcDeviceEnd(device);


    if (ret != MSG_OK){
#if TAG_RTC_STM32U3_COMPAT
        debug_log_printf("Error in rv3028 getRTCDateTime ret=%d status=0x%02x\r\n",
                         ret, status);
#else
        debug_log_printf("Error in rv3028 gettRTCDateTime\r\n");
#endif

    }
    return ret;
}
/** @} */
