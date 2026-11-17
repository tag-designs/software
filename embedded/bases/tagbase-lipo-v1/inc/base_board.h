/**
 * @file    base_board.h
 * @brief   Target-interface hooks for the LiPo Base V1, used by common stlink.c.
 *
 * @details Overrides the defaults in @c common/src/stlink.c for two ways this
 *          board differs from the breakout bases:
 *
 *          - **Reset.** PA10 reaches the tag's nRST through R104 (2.2 kOhm)
 *            with no transistor, so reset is active-low. The line is
 *            open-drain in the board file: clearing it pulls nRST low, setting
 *            it lets go and the tag's own pull-up holds nRST at the tag's
 *            1.8 V. The pin can never drive 3.3 V into a 1.8 V tag (design
 *            review item L1, @c tagbase-lipo-v1-design-review.md).
 *          - **1.8 V interface supply.** PB6 enables the AP2112 that powers the
 *            tag side of both SN74LVC1T45 level shifters. It is on only while
 *            an SWD session is being opened or is open, so the shifters do not
 *            back-power an unpowered tag, and with VCCB off they hold both tag
 *            pins in high impedance (review item L2). The board cannot tell
 *            whether a tag is powered: the TLV9001 follower on PA0 has a
 *            floating input when nothing drives TAG_3V3, so its reading is
 *            not used.
 *
 * @see     main.c, common/src/stlink.c
 */

#ifndef BASE_BOARD_H
#define BASE_BOARD_H

#include "hal.h"

/**
 * @brief   Powers the tag-side level shifters and waits for the rail.
 *
 * @details Sets EN1V8. If the rail was off, sleeps long enough for the
 *          AP2112 to reach regulation into its output capacitance before
 *          returning. Calling it with the rail already on costs nothing.
 *
 * @pre     Called from thread context after @c chSysInit().
 * @post    EN1V8 is high and the level shifters are powered.
 *
 * @warning Blocks for a few milliseconds when the rail was off. Not callable
 *          from an ISR.
 */
void tgtIoPowerOn(void);

/**
 * @brief   Removes power from the tag-side level shifters.
 *
 * @details Clears EN1V8. With VCCB off the SN74LVC1T45s put both ports in
 *          high impedance, so the tag's SWCLK and SWDIO are left to the tag's
 *          own pulls.
 *
 * @post    EN1V8 is low.
 */
void tgtIoPowerOff(void);

/** @brief Pulls the tag's nRST low through R104 (open-drain, see file note). */
#define TGT_RESET_ASSERT() palClearLine(LINE_TGT_RESET)

/** @brief Lets go of the tag's nRST; the tag's pull-up returns it high. */
#define TGT_RESET_RELEASE() palSetLine(LINE_TGT_RESET)

/** @brief Called by stlink.c before SWD_Open(); see tgtIoPowerOn(). */
#define TGT_IO_POWER_ON() tgtIoPowerOn()

/** @brief Called by stlink.c once no session is open; see tgtIoPowerOff(). */
#define TGT_IO_POWER_OFF() tgtIoPowerOff()

#endif /* BASE_BOARD_H */
