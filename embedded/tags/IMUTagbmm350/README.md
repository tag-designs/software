# IMUTagbmm350 Maintainer Notes

`IMUTagbmm350` is an IMUTag family member for the IMUTagBreakout hardware with
the AK09940A magnetometer replaced by a Bosch BMM350 breakout.

## Hardware Shape

- Board files: reuses the generated `IMUTagv1` / `board-imutag-breakout`
  board.
- Flash: same MX25L SPI NOR flash path as `IMUTagBreakout`.
- IMU: same LSM6DSV16X SPI binding and LPTIM2 trigger output as
  `IMUTagBreakout`.
- Pressure: same LPS22HH SPI binding as `IMUTagBreakout`.
- RTC and BMM350 bus: shared software I2C on the RTC pin pair.
- BMM350 INT/DRDY: PA4, exposed by the board as `LINE_IMU_TRG_TEST`.

The breakout has RTC SDA/SCL routed opposite the logical software-I2C
assignment. Keep `SWAP_I2C` defined in `inc/custom.h`; the BMM350 descriptor in
the IMUTag family device table intentionally uses the same swapped line policy
as the default RV3028 binding.

## Build Membership

`project.mk` selects:

```make
rtc_rv3028
flash_mx25l
sensor_pressure_lps22hh
sensor_mag_bmm350
sensor_imu_lsm6dsv16x
```

The target includes `../families/IMUTag/family.mk`, so `config.c`,
`datalog.c`, `devices.c`, `sensors.c`, `state_run.c`, and `stubs.c` come from
`embedded/tags/families/IMUTag/src`. There is no local `src/sensors.c` because
the collection flow is the normal IMUTag flow; only the magnetometer module and
descriptor differ.

## BMM350 Binding

The BMM350 binding lives in the shared IMUTag family device table behind
`TAG_SENSOR_MAG_BMM350`:

- `TAG_MAG_DEVICE` becomes `&tagImuTagBmm350Device`.
- The device uses `tagRtcI2cController` from the RV3028 module so RTC and BMM350
  transactions share one software-I2C mutex.
- `BMM350_I2C_ADDRESS` defaults to `0x14` and is set explicitly in
  `inc/custom.h`.
- The interrupt mode is active-high push-pull. If the breakout wiring or
  Bosch INT configuration changes, update `tagImuTagBmm350Device` and the
  data-ready polarity together.

## Bring-Up Checks

Useful quick checks after flashing:

1. Run RTC self-test.
2. Run external flash self-test; it should exercise the MX25L path, not NAND.
3. Run the magnetometer self-test; current protocol result names still reuse
   the legacy `RUN_MMC5633` request and `AK09940A_FAILED` failure enum.
4. Enter calibration and confirm BMM350 samples are visible before writing
   calibration constants.

If BMM350 calibration works but RTC fails, inspect the shared software-I2C
line swap. If RTC works but BMM350 fails, check address selection and PA4 DRDY
polarity before changing the generic driver.
