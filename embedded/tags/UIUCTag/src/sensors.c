/**
 * @file sensors.c
 * @brief UIUCTag collection-sensor configuration and sampling.
 * @author tag firmware authors
 * @date 2026-08-31
 *
 * @details Implements the pressure half of UIUCTag acquisition: BMP585
 *          forced-mode sampling, stored as the sensor's raw data registers.
 *          The accelerometer half, initDataCollection(), lives in
 *          accel_adxl362.c (UIUCTag) or accel_adxl367.c (UIUCadxl367), and
 *          project.mk compiles the one that matches the fitted part. The
 *          RUNNING state handler in state_run.c uses only the sensors.h
 *          interface, which is what keeps the eventual LPS_RDY interrupt work
 *          confined to this file.
 *
 * @note    The BMP585 binding to SPI1 lives in the tag-local devices.c
 *          descriptors, so this file is transport-agnostic.
 *
 * @see     ../design/data-collection.md
 */

#include "hal.h"
#include <stdbool.h>
#include <string.h>

#include "tag.pb.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"
#include "uiuctag_log_format.h"

#include "bmp581.h"

/** Output data rate used for the forced-mode pressure conversion. */
#define UIUCTAG_PRESSURE_ODR BMP581_ODR_50HZ
/** DRDY poll budget for one forced conversion, in microseconds. */
#define UIUCTAG_PRESSURE_TIMEOUT_US 100000U

/*
 * Push-pull/pulsed/active-high (instead of the forced-mode default, open-
 * drain/latched/active-low) was used to drive a clean DRDY level with no
 * dependency on an external pull-up, for observing it on a scope via the
 * exposed LPS_RDY test point -- confirmed working, but an interrupt-driven
 * wait (WFI, waking on this line instead of polling INT_STATUS) was
 * evaluated and deferred (no already-proven hardware timeout reaches the
 * NVIC to safely bound such a wait on this project, which has no
 * watchdog), so nothing in this driver ever reads or reacts to this pin.
 * Left here, commented out, in case that changes.
static const bmp581_interrupt_config_t uiuctag_pressure_interrupt_config =
    BMP581_INTERRUPT_PUSH_PULL_PULSED_ACTIVE_HIGH;
 */

/* Public API contract documented in sensors.h. */
bool samplePressureRaw(t_UIUCTagRecord *record)
{
  uint8_t raw[6];
  uint32_t pressure;
  int rc;

  /*
   * UIUCTag fully powers the BMP585 rail off between samples, so every call
   * is a genuine power-on reset -- exactly the precondition
   * bmp581_config_forced_fast_device() requires (see its doc comment). Cuts
   * config from 24 SPI transactions to 9.
   */
  rc = bmp581_config_forced_fast_device(TAG_PRESSURE_DEVICE,
                                        UIUCTAG_PRESSURE_ODR,
                                        NULL);
  if (rc == 0)
    rc = bmp581_sample_forced_raw_blocking_device(TAG_PRESSURE_DEVICE,
                                                  UIUCTAG_PRESSURE_TIMEOUT_US,
                                                  raw);

  /*
   * Power down on every path. bmp581_config_forced_device() leaves the rail on
   * so a caller may sleep until DRDY, so a failure between configuration and
   * readout must not leak that power into standby.
   */
  tagBusPowerOff(&TAG_PRESSURE_DEVICE->registers->bus);
  tagPressureDeviceAfterPowerOff(TAG_PRESSURE_DEVICE);

  /* raw[] is in register order: temperature (0x1D..0x1F), pressure (0x20..0x22). */
  pressure = (rc == 0) ? uiuctagGet24(&raw[3]) : 0U;

  /* The range check also rejects a bus that read all ones. */
  if ((rc != 0) || (pressure < UIUCTAG_PRESSURE_RAW_MIN) ||
      (pressure > UIUCTAG_PRESSURE_RAW_MAX))
  {
    uiuctagPut24(record->pressure, UIUCTAG_RAW_FAILED);
    uiuctagPut24(record->temperature, UIUCTAG_RAW_FAILED);
    return false;
  }

  uiuctagPut24(record->pressure, pressure);
  memcpy(record->temperature, &raw[0], 3U);
  return true;
}
