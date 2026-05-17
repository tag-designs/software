#ifndef TAG_CORE_POWER_H
#define TAG_CORE_POWER_H

#include <stdint.h>

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
