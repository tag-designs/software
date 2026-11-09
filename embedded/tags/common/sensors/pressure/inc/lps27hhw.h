/**
 * @file lps27hhw.h
 * @brief Public API for descriptor-backed ST LPS27HHW pressure sensors.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef LPS27HHW_H
#define LPS27HHW_H

#include "lps.h"

#include <stdbool.h>
#include <stdint.h>

/** @name LPS27HHW pressure API
 * Descriptor-backed helpers for one-shot pressure/temperature reads and test.
 * @{
 */
/**
 * @brief Trigger and read one pressure/temperature sample.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] pressure Raw pressure output.
 * @param[out] temperature Raw temperature output.
 * @return true when a fresh sample was read.
 */
bool lps27GetPressureTemp(const TagPressureDevice *device, int16_t *pressure,
                          int16_t *temperature);

/** @brief Bytes returned by lps27GetPressureTempFull(): 24-bit pressure,
 *         16-bit temperature, in register order. */
#define LPS27_FULL_SAMPLE_BYTES 5

/**
 * @brief Trigger one conversion and read all five output registers.
 *
 * @details Same one-shot sequence as lps27GetPressureTemp(), but the burst
 *          read starts at PRESS_OUT_XL (0x28), so the pressure keeps its low
 *          byte: 24 bits at 4096 LSB/hPa instead of 16 at 16 LSB/hPa. The
 *          noise mode is chosen per call rather than by LPS_LOW_POWER, so a
 *          stored configuration can select it. Readiness is polled up to
 *          LPS27_READY_POLLS times in low-power mode and
 *          LPS27_LOW_NOISE_READY_POLLS in low-noise mode, LPS27_READY_POLL_MS
 *          apart; the loop leaves on the first ready status.
 *
 *          lps27GetPressureTemp() is deliberately left as it was: it is the
 *          path qualified on PresTagRaw and BitPresTag, and PresTag still uses
 *          it when neither LPS27 option is configured.
 *
 * @param[in]  device    Pressure device descriptor.
 * @param[in]  low_noise true to set CTRL_REG2.LOW_NOISE_EN for this conversion.
 * @param[out] raw       PRESS_OUT_XL, PRESS_OUT_L, PRESS_OUT_H, TEMP_OUT_L,
 *                       TEMP_OUT_H as read. On failure the pressure bytes are
 *                       0x00 0x00 0x80 (24-bit minimum) and the temperature
 *                       0x8000, so a failed read is distinguishable from data
 *                       and from erased flash.
 * @return true when a fresh, non-overrun sample was read.
 *
 * @pre    The sensor is powered off (the call powers it and removes power).
 * @warning Blocks for the power-up delay and the readiness polls, sleeping in
 *          Stop between them; not callable from an ISR.
 */
bool lps27GetPressureTempFull(const TagPressureDevice *device, bool low_noise,
                              uint8_t raw[LPS27_FULL_SAMPLE_BYTES]);

/**
 * @brief Convert a full-resolution raw pressure to hectopascals.
 *
 * @param[in] pressure Sign-extended 24-bit raw pressure.
 * @return Pressure in hPa (4096 LSB/hPa).
 */
float lps27PressureFull(int32_t pressure);

/**
 * @brief Verify identity and sample readiness for an LPS27HHW device.
 *
 * @param[in] device Pressure device descriptor.
 * @return true when identity and sample read pass.
 */
bool lps27Test(const TagPressureDevice *device);

/**
 * @brief Convert raw pressure to hectopascals.
 *
 * @param[in] pressure Raw pressure output.
 * @return Pressure in hPa.
 */
float lps27Pressure(int16_t pressure);

/**
 * @brief Convert raw temperature to degrees Celsius.
 *
 * @param[in] temperature Raw temperature output.
 * @return Temperature in degrees Celsius.
 */
float lps27Temperature(int16_t temperature);
/** @} */

#endif
