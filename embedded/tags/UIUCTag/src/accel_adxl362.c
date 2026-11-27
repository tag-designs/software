/**
 * @file accel_adxl362.c
 * @brief UIUCTag accelerometer: ADXL362 wake-mode activity detection.
 * @author tag firmware authors
 * @date 2026-11-27
 *
 * @details initDataCollection() for boards fitted with an ADXL362, which is
 *          what the UIUCTag prototypes carry. The ADXL367 version, for the
 *          UIUCadxl367 target, is accel_adxl367.c; project.mk compiles one or
 *          the other. The two parts are pin compatible, so the board, its
 *          USART2 binding and the INT1 wake line are the same for both.
 *
 *          Both files implement the same contract against the same stored
 *          configuration, so the host sees one UIUCTag configuration whichever
 *          part is fitted:
 *          - the activity threshold, @c sconfig.adxl_act_thresh_cnt, is in mg
 *            (config.c clamps it to 200..700 mg);
 *          - inactivity is @c sconfig.adxl_inactive_samples samples at the
 *            wake-up rate (config.c clamps it to 3..12);
 *          - the inactivity threshold is a fixed 1100 mg, absolute.
 *
 *          The configuration mirrors the ADXL367 one register for register:
 *          referenced activity, absolute inactivity, loop mode, AWAKE on
 *          INT1, wake-up mode. Only the encodings differ. At the +/-2 g range
 *          the ADXL362's threshold registers count 1 mg per LSB in an 11-bit
 *          field, so mg are written as they are (the ADXL367 needs 4 LSB/mg,
 *          shifted). In wake-up mode the ADXL362 samples about 6 times a
 *          second whatever FILTER_CTL's rate says, close to the ADXL367's
 *          configured 6.25 Hz, so an inactivity count means about the same
 *          time on either part. Its activity timer is ignored in wake-up mode:
 *          one sample over threshold is activity.
 *
 * @note    The ADXL362 sits on USART2 in synchronous 4-wire SPI mode; the
 *          binding lives in devices.c, so this file is transport-agnostic.
 *
 * @see     ../design/data-collection.md
 */

#include "hal.h"
#include <stdbool.h>

#include "tag.pb.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"

#include "ADXL362.h"

/** Measurement range; makes the threshold registers count 1 mg per LSB. */
#define UIUCTAG_ADXL_RANGE ADXL362_RANGE_2G
/** Nominal rate; wake-up mode overrides it with its own ~6 Hz. */
#define UIUCTAG_ADXL_ODR ADXL362_ODR_12_5_HZ
/** FILTER_CTL: +/-2 g, quarter-rate anti-alias filter, 12.5 Hz. */
#define UIUCTAG_ADXL_FILTER_CTL                                              \
  (ADXL362_FILTER_CTL_RANGE(UIUCTAG_ADXL_RANGE) | ADXL362_FILTER_CTL_HALF_BW | \
   ADXL362_FILTER_CTL_ODR(UIUCTAG_ADXL_ODR))
/** Activity time in samples; ignored in wake-up mode, kept equal to the ADXL367's. */
#define UIUCTAG_ADXL_ACT_COUNT 2
/** Absolute inactivity threshold, mg; config.c reports the same value. */
#define UIUCTAG_ADXL_INACT_THRESH_MG 1100U
/** Largest value the 11-bit THRESH_ACT/THRESH_INACT fields hold. */
#define UIUCTAG_ADXL_THRESH_MAX 0x07FFU
/** ACT_INACT_CTL: loop mode, referenced activity, absolute inactivity. */
#define UIUCTAG_ADXL_ACT_INACT_CTL                                           \
  (ADXL362_ACT_INACT_CTL_LINKLOOP(ADXL362_MODE_LOOP) |                       \
   ADXL362_ACT_INACT_CTL_INACT_EN | ADXL362_ACT_INACT_CTL_ACT_REF |           \
   ADXL362_ACT_INACT_CTL_ACT_EN)
/** Settling time after a soft reset before the part is written, ms. */
#define UIUCTAG_ADXL_RESET_SETTLE_MS 10

_Static_assert(UIUCTAG_ADXL_INACT_THRESH_MG <= UIUCTAG_ADXL_THRESH_MAX,
               "UIUCTag inactivity threshold does not fit THRESH_INACT at +/-2 g");

/* Public API contract documented in sensors.h. */
void initDataCollection(void)
{
  uint16_t act_thresh = sconfig.adxl_act_thresh_cnt;
  uint8_t status;

  if (act_thresh > UIUCTAG_ADXL_THRESH_MAX)
    act_thresh = UIUCTAG_ADXL_THRESH_MAX;

  ADXL362_DeinitDevice(TAG_ACCEL_DEVICE);

  ADXL362_DeviceBegin(TAG_ACCEL_DEVICE);

  /*
   * The accelerometer is not on an MCU-switched rail, so its loop-mode
   * activity state survives every MCU reset. On the ADXL367 a stale "awake"
   * state was seen holding the wake line asserted from boot; a soft reset
   * gives a known starting state independent of that history.
   */
  ADXL362_SoftwareResetDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(UIUCTAG_ADXL_RESET_SETTLE_MS);

  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 ADXL362_POWER_CTL_MEASURE(ADXL362_MEASURE_STANDBY),
                                 ADXL362_REG_POWER_CTL, 1);

  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_FILTER_CTL,
                                 ADXL362_REG_FILTER_CTL, 1);

  // Activity: the configured threshold, referenced to the resting attitude.
  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, act_thresh,
                                 ADXL362_REG_THRESH_ACT_L, 2);
  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_ACT_COUNT,
                                 ADXL362_REG_TIME_ACT, 1);

  // Inactivity: every axis under 1100 mg, absolute, for the configured count.
  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_INACT_THRESH_MG,
                                 ADXL362_REG_THRESH_INACT_L, 2);
  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 sconfig.adxl_inactive_samples,
                                 ADXL362_REG_TIME_INACT_L, 2);

  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, UIUCTAG_ADXL_ACT_INACT_CTL,
                                 ADXL362_REG_ACT_INACT_CTL, 1);

  // AWAKE as an active-high level on INT1, the board's MCU wakeup line.
  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE, ADXL362_INTMAP1_AWAKE,
                                 ADXL362_REG_INTMAP1, 1);

  ADXL362_SetRegisterValueDevice(TAG_ACCEL_DEVICE,
                                 ADXL362_POWER_CTL_WAKEUP |
                                     ADXL362_POWER_CTL_MEASURE(ADXL362_MEASURE_ON),
                                 ADXL362_REG_POWER_CTL, 1);

  /*
   * Read STATUS after the standby-to-measure transition, as the ADXL367
   * version does: without it that part's AWAKE pin was seen stuck asserted
   * until a real shake. Reading STATUS also clears the activity and
   * inactivity flags.
   */
  ADXL362_GetRegisterValueDevice(TAG_ACCEL_DEVICE, &status,
                                 ADXL362_REG_STATUS, 1);
  (void)status;

  ADXL362_DeviceEnd(TAG_ACCEL_DEVICE);
}
