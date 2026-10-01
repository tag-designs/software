/* stub: the register blocks and kernel calls datalog.c touches. */
#ifndef STUB_HAL_H
#define STUB_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t ACR; } stub_flash_t;
typedef struct { uint32_t CR; } stub_rcc_t;
typedef struct { uint32_t PSC; } stub_tim_t;
extern stub_flash_t stub_flash;
extern stub_rcc_t stub_rcc;
extern stub_tim_t stub_tim;
#define FLASH (&stub_flash)
#define RCC (&stub_rcc)
#define STM32_ST_TIM (&stub_tim)
#define NOINIT
#define HIGHPRIO 255
#define NORMALPRIO 128
static inline void chSysLock(void) {}
static inline void chSysUnlock(void) {}
static inline void chThdYield(void) {}
static inline void chThdSetPriority(int p) { (void)p; }
#endif
