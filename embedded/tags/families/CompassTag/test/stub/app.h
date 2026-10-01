/* stub: the runtime umbrella header; the harness defines what it provides. */
#ifndef STUB_APP_H
#define STUB_APP_H
#include <stdint.h>
#include "hal.h"
#include "core_state.h"
extern int32_t timestamp;
extern uint32_t events;
extern bool isActive;
#define EVT_RTC_WUTF 0x1u
void disableAllAlarms(void);
void disableTicker(void);
void enableTicker(uint16_t interval);
void adcVDD(uint16_t *vdd100, int16_t *temp10);
bool tagCompassAccelWakeActive(void);
#endif
