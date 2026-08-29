# BitPresTag BMP581/BMP585 Forced-Mode Pressure Plan

## Scope

This note plans the firmware changes needed for a BitPresTag hardware variant
that replaces the LPS27 pressure sensor with a Bosch BMP581-compatible BMP585.
The BMP585 is treated as software-compatible with the existing BMP581 driver.

The revised board connects the pressure interrupt line. The pressure path
should therefore move from the current timed/polling one-shot model to
forced-mode sampling paced by RTC wakes and completed by the BMP58x data-ready
interrupt.

## Existing BitPresTag Model

The current BitPresTag family uses `lps27GetPressureTemp()` from the RUNNING
state whenever the RTC wakeup timer fires. That helper powers the pressure
sensor, starts a one-shot conversion, polls the sensor status register, reads
the sample, and powers the sensor back off before returning.

This is simple but keeps the MCU awake during pressure conversion. It also
assumes the pressure interrupt line is unavailable.

## BMP58x Driver Support

The shared BMP581 driver should provide these descriptor-backed operations:

- configure forced-mode pressure/temperature conversion and leave the sensor
  powered in standby;
- configure data-ready interrupt output as latched, active-low, open-drain for
  pull-up biased wake lines;
- trigger one forced-mode conversion with `BMP5_POWERMODE_FORCED`;
- read and clear `INT_STATUS` without powering the sensor down;
- read one compensated pressure/temperature sample without powering the sensor
  down;
- provide a blocking forced-sample helper only for self-tests and diagnostics.

The driver must not know which MCU wake line carries the BMP58x interrupt. That
mapping belongs in the BitPresTag variant board/custom layer and family device
code.

## Reset And Readiness

The new board reports pressure-chip readiness with a power-on-reset interrupt.
Use that line as a wake/readiness hint, but still verify Bosch status through
the BMP5 API before accepting the device as initialized.

The BMP581/BMP585 SPI path still requires the reset-time dummy read before
register data is reliable. Keep the existing soft-reset recovery path for
`BMP5_E_NVM_NOT_READY` when chip ID and core-ready status are sane.

Because the interrupt is latched by default, firmware must read `INT_STATUS`
after any POR or DRDY wake to clear the asserted line and decode the wake cause.

## RUNNING-State Sequence

The BitPresTag BMP58x variant should split pressure acquisition into two
state-machine phases:

1. RTC sample wake:
   - update activity accounting and voltage tracking;
   - power and configure the BMP58x if needed;
   - trigger one forced-mode conversion;
   - enable the BMP58x DRDY/POR wake source;
   - return to low power without writing a pressure record yet.

2. BMP58x interrupt wake:
   - read and clear `INT_STATUS`;
   - if POR/reset-ready is set, complete initialization and trigger a sample;
   - if DRDY is set, read pressure/temperature without powering down first;
   - write the BitPresTag activity/pressure/temperature record;
   - power down or leave the BMP58x in standby according to the measured energy
     tradeoff;
   - return to the normal RTC-paced wait.

The family state should remember that a pressure sample is pending between the
RTC trigger wake and the later DRDY wake. If activity bits are accumulated
during that interval, preserve the snapshot that belongs to the sample period
so the logged activity and pressure record remain aligned.

## Device And Target Integration

A BMP58x BitPresTag target should:

- select `sensor_pressure_bmp581` instead of `sensor_pressure_lps27`;
- bind the pressure descriptor to the BMP58x SPI bus, chip select, power rail,
  and dummy byte;
- define a pressure interrupt wake source separate from the ADXL362 wake line;
- install `tag_test_bmp581` for `RUN_LPS`;
- provide a compile-time selector such as `BITPRESTAG_PRESSURE_BMP581_FORCED`
  so shared family code can preserve the existing LPS27 behavior.

The existing `BitPresTag` and `BitPresTagMX25R` LPS27 targets should not change
behavior while the BMP58x variant is brought up.

## Validation

Initial validation should proceed in this order:

1. `RUN_LPS` confirms BMP58x chip ID and one blocking forced sample.
2. A GPIO wake smoke test triggers forced mode and confirms the MCU wakes from
   the BMP58x interrupt line.
3. A short RUNNING capture confirms each RTC period produces exactly one
   activity/pressure/temperature log record.
4. A reset recovery test confirms POR-ready and soft-reset recovery both clear
   the latched interrupt line by reading `INT_STATUS`.
5. Power testing decides whether the sensor should be powered down after each
   sample or left in standby between sample periods.

## Open Questions

- Which STM32 wake source will carry the BMP58x DRDY/POR line?
- Is the interrupt pull-up external, internal, or both?
- Should the first BMP58x target replace the existing `BitPresTag` name or use
  a distinct variant name during bring-up?
- Is pressure energy lower when powering down every sample, or when leaving the
  BMP58x in standby and avoiding repeated boot/configure cycles?
