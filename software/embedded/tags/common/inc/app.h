#ifndef APP_H
#define APP_H
#include "ch.h"
#include "hal.h"
#include "monitor.h"
#include "rv8803.h"
#include "rv3028.h"
#include "rv3032.h"
#include "tag.pb.h"
#include "custom.h"
#include "mcuconf.h"

// Compile time checks

#define CASSERT(predicate) _impl_CASSERT_LINE(predicate, __LINE__, __FILE__)
#define _impl_PASTE(a, b) a##b
#define _impl_CASSERT_LINE(predicate, line, file)      \
  typedef char _impl_PASTE(assertion_failed_##file##_, \
                           line)[2 * !!(predicate)-1];

// attribute for uninitialize data (not bss or data)
#define NOINIT __attribute__((section(".ram0")))
// detect connected monitor/debugger
#define MONCONNECTED (CoreDebug->DEMCR & CoreDebug_DEMCR_VC_CORERESET_Msk)

enum Sleep { SHUTDOWN, STANDBY,STOP2, SLEEP};
extern thread_t *tpMain;     // main thread
enum Sleep StateMachine(void);     // main thread loop
void CheckEvents(void);      // main thread event check
void deviceInit(int force);  // device initialization
void godown(enum Sleep mode);    // power managment

// global state for activity detection

extern bool isActive;

//
// Pin modification functions
//
// We assume the alterate function number is set in board.h
// so mode configuration is a matter of setting a few bits
//

static inline void toInput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 0);
}

static inline void toOutput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 1 << (pin * 2));
}

static inline void toAlternate(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 2 << (pin * 2));
}

static inline void toAnalog(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 3 << (pin * 2));
}

//  Internal Flash

void FLASH_Lock(void);
void FLASH_Unlock(void);
void FLASH_PageErase(uint32_t Page);
void FLASH_Program_DoubleWord(uint32_t *Address, uint32_t Data0,
                              uint32_t Data1);
void FLASH_Flush_Data_Cache(void);
uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words);

// Accel

void accelSpiOn(void);
void accelSpiOff(void);
int accelConfigureFIFO(int entries);
extern uint8_t adxl_status;

// ADC

void adcVDD(uint16_t *vdd100, int16_t *temp10);
void adc1Start(void);
void adc1Stop(void);
int adc1StartConversion(uint16_t channel, uint16_t delay);
bool adc1Eoc(void);
uint32_t adc1DR(void);
void adc1StopConversion(void);
void adc1EnableVREF(void);
void adc1DisableVREF(void);
void adc1EnableTS(void);
void adc1DisableTS(void);

// RTC

void rtcOn(void);
void rtcOff(void);
int rv8803_GetReg(enum RV8803Reg reg, uint8_t *val, int num);
int rv3028_GetReg(enum RV3028Reg reg, uint8_t *val, int num);
//int rv3028_EEPROM_Exec(unsigned char addr, unsigned char *val, unsigned char cmd);
//void setRTCUnixEpoch(int32_t epoch);
//void getRTCUnixEpoch(int32_t *epoch);
msg_t getRTCDateTime(RTCDateTime *tm);
msg_t setRTCDateTime(RTCDateTime *tm);
// Time functions

int32_t GetTimeUnixSec(uint32_t *millis);
int SetTimeUnixSec(int32_t unix_time);

enum ALARM_TYPE {ALARM_SECOND, ALARM_MINUTE, ALARM_HOUR};
void enableAlarm(unsigned int alarm, enum ALARM_TYPE atype);
void disableAlarm(unsigned int alarm);
void disableAllAlarms(void);
void stopMilliseconds(bool spiEnabled, unsigned int);
bool initRTC(void);
void enableTicker(uint16_t);
void disableTicker(void);

#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE  == (32*1024)
#define RV3028_CLKOUT_VAL 0
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE  == 8192
#define RV3028_CLKOUT_VAL 1
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE  == 1024
#define RV3028_CLKOUT_VAL 2
#endif
#if STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE  == 64
#define RV3028_CLKOUT_VAL 3
#endif

// Test

void test(void);
TestResult testreport(void);

// Events

extern eventmask_t events;

// ADXL

#define EVT_ADXL_DATA_READY EVENT_MASK(0)
#define EVT_ADXL_FIFO_READY EVENT_MASK(1)
#define EVT_ADXL_FIFO_WATERMARK EVENT_MASK(2)
#define EVT_ADXL_FIFO_OVERRUN EVENT_MASK(3)
#define EVT_ADXL_ACT EVENT_MASK(4)
#define EVT_ADXL_INACT EVENT_MASK(5)
#define EVT_ADXL_AWAKE EVENT_MASK(6)
#define EVT_ADXL_MASK(x) ((x)&0x7f)

// Generic wakeup event

#define EVT_WKUP EVENT_MASK(7)

// STM32 RTC -- these match the status bit numbers !!
//           -- only define those we might use

#define EVT_RTC_ALRAF EVENT_MASK(8)
#define EVT_RTC_ALRBF EVENT_MASK(9)
#define EVT_RTC_WUTF EVENT_MASK(10)
#define EVT_RTC_MASK(x) ((x) & (0x7 << 8))

// Standby/shutdown

#define EVT_WAKE_STANDBY EVENT_MASK(11)

// Monitor command events

#define EVT_START EVENT_MASK(12)
#define EVT_STOP  EVENT_MASK(13)
#define EVT_RESET EVENT_MASK(14)
#define EVT_SELFTEST EVENT_MASK(15)
#define EVT_CALIBRATE EVENT_MASK(16)
#define EVT_MONITOR_ALL (EVT_START | EVT_STOP | EVT_RESET | EVT_SELFTEST | EVT_CALIBRATE)

// State from main.c

//extern mutex_t ADCmutex;
//extern mutex_t SPImutex;
extern binary_semaphore_t ADCmutex;
extern binary_semaphore_t I2Cmutex;
extern binary_semaphore_t SPImutex;
extern int32_t timestamp;
extern uint32_t timestamp_millis;
extern thread_t *tpMain;

// State machine

enum StateTrans
{
  T_INIT,
  T_CONT,
  T_ERROR
};

extern enum Sleep Configured(enum StateTrans,  State_Event reason);
extern enum Sleep Running(enum StateTrans,  State_Event reason);
extern enum Sleep Finished(enum StateTrans, State_Event reason);
extern enum Sleep Aborted(enum StateTrans, State_Event reason);
extern enum Sleep Hibernating(enum StateTrans,  State_Event reason);

// Tests

extern TestReq test_to_run;

void spi1On(void);
void spi1Off(void);

// Accel (generic)

void accelOn(void);
void accelOff(void);

// Sensor Calibration

bool sensorSample(SensorData *sensors);
bool initSensors(void);
bool deinitSensors(void);

#endif
