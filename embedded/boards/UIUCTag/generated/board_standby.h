/*
 * Generated from board-customizations.json. Do not edit by hand.
 */

#ifndef BOARD_STANDBY_H
#define BOARD_STANDBY_H

#define BOARD_STANDBY_HAS_CONFIG 1

#define HAS_PULLUPA 1
#define HAS_PULLDWNA 1
#define PULLUPA ((1U << 1U) | \
  (1U << 15U))
#define PULLDWNA ((1U << 2U) | \
  (1U << 3U) | \
  (1U << 4U) | \
  (1U << 5U) | \
  (1U << 11U) | \
  (1U << 12U))

#define HAS_PULLUPB 1
#define HAS_PULLDWNB 1
#define PULLUPB ((1U << 6U) | \
  (1U << 7U))
#define PULLDWNB ((1U << 0U) | \
  (1U << 1U) | \
  (1U << 3U) | \
  (1U << 4U) | \
  (1U << 5U))

#define HAS_PULLUPC 0
#define HAS_PULLDWNC 0
#define PULLUPC 0U
#define PULLDWNC 0U

#endif
