# IMUTagNandBmp581 Maintainer Notes

`IMUTagNandBmp581` is the STM32U375/Cortex-M33 NAND build for the revised
IMUTagNand replacement board. It keeps the active IMUTag protocol family while
using the BMP581 pressure sensor, BMM350 magnetometer, LSM6DSV16X IMU,
generated `IMUTagNandv2` board files, and the 2 Gbit GD5F SPI-NAND storage
module.

## Hardware Shape

- Board files: generated `IMUTagNandv2` / `board-imutag-nand-v2`, configured
  as an STM32U375 board.
- Pressure header mapping: PA9 is BMP581 `LPS_DRDY`; PA10 is BMP581
  `LPS_CS`. The board customization JSON follows the breakout silkscreen and
  must not be swapped back to the early bring-up note.
- Flash: GD5F2GM7RE SPI-NAND using the common logical block map in STM32
  internal flash.
- IMU: LSM6DSV16X SPI binding with the external ODR trigger on PB4
  (`LINE_LSM_TRG`) driven by LPTIM1 channel 2.
- Pressure: BMP581 SPI binding from the IMUTag family.
- RTC and BMM350 bus: shared software I2C on the RTC pin pair.
- BMM350 INT/DRDY: PB5, exposed by the board as `LINE_BMM_INT`.
- Test outputs: PA1 is `LINE_LED1`; PA2 is `LINE_testpin`.
  STOP/WFI diagnostic driving is disabled by default; define
  `TAG_IDLE_STOP_DIAGNOSTICS` for idle-hook probes or
  `TAG_MAIN_SLEEP_DIAGNOSTICS` for main-loop sleep probes.

The target uses the shared STM32U375 linker script. That script reserves the
last two STM32 flash pages for provisioned configuration: calibration constants
in the penultimate page and the NAND logical block map in the final page.
Shared IMUTag VDD/header records stop at `__persistent_end__` before those
pages.

## Build Membership

`project.mk` selects:

```make
debug_log
rtc_rv3028
flash_gd5f2gm7re
sensor_pressure_bmp581
sensor_mag_bmm350
sensor_imu_lsm6dsv16x
```

The target includes `../families/IMUTag/family.mk`, so shared application
sources come from `embedded/tags/families/IMUTag/src`. The local
`src/hal_rtc_lld.c` and `src/power_modes.c` provide the U375 RTC shim and
idle-hook implementation for this target.

## Bring-Up Status

Bench validation on the first BMP581 breakout confirmed:

- `RUN_EXT_FLASH` passes against the GD5F2GM7RE SPI-NAND after using the
  shared GD5F self-test path for both 1 Gbit and 2 Gbit variants.
- `RUN_LPS` passes against BMP581 after correcting the PA9/PA10
  data-ready/chip-select assignment.
- A short 400 Hz IMU collection produced SQLite `ImuAccel`, `ImuGyro`,
  `ImuMag`, `ImuPressure`, `ImuTemperature`, and `Calibration` tables with
  plausible pressure and pressure-temperature values.

The `debug_log` module remains enabled while this target is under bring-up so
BMP581 and NAND diagnostics can be captured by `tag-test --debug` and
qtmonitor.
