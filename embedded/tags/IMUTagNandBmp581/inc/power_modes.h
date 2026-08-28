
/**
 * @file power_modes.h
 * @brief IMUTagNandBmp581 idle-thread low-power hook declarations.
 *
 * @details These hooks are called from the ChibiOS idle path. They select
 *          STOP-mode entry when the monitor is detached and fall back to WFI
 *          sleep while the monitor is attached so host interactions remain
 *          responsive.
 */

#ifndef POWER_MODES_H
#define POWER_MODES_H

/* This guard forces the assembler to ignore the C functions below */
#if !defined(__ASSEMBLY__) && !defined(__ASSEMBLER__)

/**
 * @brief Prepare the idle path for the currently selected low-power mode.
 *
 * @warning Called from the idle thread; it must not block.
 */
void idle_enter(void);

/**
 * @brief Execute one idle wait instruction using the selected sleep depth.
 *
 * @warning Called repeatedly from the idle thread.
 */
void idle_loop(void);

/**
 * @brief Restore CPU sleep-depth state after an idle wait returns.
 */
void idle_leave(void);

/**
 * @brief Force the idle path to use WFI sleep instead of STOP mode.
 */
void idle_enable_wfi_sleep(void);

#endif
#endif /* POWER_MODES_H */
