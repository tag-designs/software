#ifndef TAG_CORE_POWER_H
#define TAG_CORE_POWER_H

#include "hal.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "usart_bus.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_NO_LINE ((ioline_t)0)

bool tagLineIsValid(ioline_t line);
void tagDisableActiveBusesForStop(void);
void tagEnableActiveBusesAfterStop(void);
void tagEnableStandbyPullup(ioline_t line);
void tagEnableStandbyPulldown(ioline_t line);
void tagPrepareDevicesForStandby(void);

void rtcOn(void);
void rtcOff(void);

void spi1On(void);
void spi1Off(void);

void accelSpiOn(void);
void accelSpiOff(void);
int accelConfigureFIFO(int entries);
extern uint8_t adxl_status;

void accelOn(void);
void accelOff(void);

void FlashSpiOn(void);
void FlashSpiOff(void);

#endif
