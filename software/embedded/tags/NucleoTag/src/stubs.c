#include "hal.h"
#include "app.h"
#include "tagdata.pb.h"
#include "config.h"
#include "persistent.h"
#if 0
#include "ADXL362.h"

void ADXL362_SetRegisterValue(unsigned short registerValue,
                              unsigned char registerAddress,
                              unsigned char bytesNumber)
{
    (void)registerValue;
    (void)registerAddress;
    (void)bytesNumber;
}

/*! Performs a burst read of a specified number of registers. */
void ADXL362_GetRegisterValue(unsigned char *pReadData,
                              unsigned char registerAddress,
                              unsigned char bytesNumber)
{
    (void)pReadData;
    (void)registerAddress;
    (void)bytesNumber;
}

void ADXL362_SoftwareReset(void) {}

void ADXL362_SetupActivityDetection(unsigned char refOrAbs,
                                    unsigned short threshold,
                                    unsigned char time)
{
    (void)refOrAbs;
    (void)threshold;
    (void)time;
}

/*! Configures inactivity detection. */
void ADXL362_SetupInactivityDetection(unsigned char refOrAbs,
                                      unsigned short threshold,
                                      unsigned short time)
{
    (void) refOrAbs;
    (void) threshold;
    (void) time;
}

void accelSpiOn(void){}
void accelSpiOff(void){}
#endif

msg_t setRTCDateTime(RTCDateTime *tim)
{
    rtcSetTime(&RTCD1,tim);
    return MSG_OK;
}

msg_t getRTCDateTime(RTCDateTime *tim)
{
    rtcGetTime(&RTCD1,tim);
    return MSG_OK;
}

bool initRTC(void)
{

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
    return true;
}
