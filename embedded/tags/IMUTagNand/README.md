# IMUTagNand Maintainer Notes

`IMUTagNand` is the STM32U375/Cortex-M33 NAND build derived from
`IMUTagU3bmm350`. It keeps the U375 RTCv3 shim, Stop1 support, BMM350
magnetometer, LSM6DSV16X IMU, and LPS22HH pressure sensor runtime shape from
that target, but uses the generated `IMUTagNandv1` board files and replaces the
MX25U12843 SPI NOR storage module with the GD5F SPI-NAND storage module.

## Hardware Shape

- Board files: generated `IMUTagNandv1` / `board-imutag-nand-v1`, configured
  as an STM32U375 board.
- Flash: GD5F SPI-NAND using the common logical block map in STM32 internal
  flash.
- IMU: LSM6DSV16X SPI binding and LPTIM2 trigger output from the IMUTag
  family.
- Pressure: LPS22HH SPI binding from the IMUTag family.
- RTC and BMM350 bus: shared software I2C on the RTC pin pair.
- BMM350 INT/DRDY: PB5, exposed by the board as `LINE_BMM_INT`.
- Test outputs: PA1 is `LINE_LED1`; PA2 is `LINE_testpin`.
  STOP/WFI diagnostic driving is disabled by default; define
  `TAG_IDLE_STOP_DIAGNOSTICS` only when probing those lines.

The target uses the shared STM32U375 linker script. That script reserves the
last two STM32 flash pages for provisioned configuration: calibration constants
in the penultimate page and the NAND logical block map in the final page.
Shared IMUTag VDD/header records stop at `__persistent_end__` before those
pages.

## Build Membership

`project.mk` selects:

```make
rtc_rv3028
flash_gd5f1gq5re
sensor_pressure_lps22hh
sensor_mag_bmm350
sensor_imu_lsm6dsv16x
```

The target includes `../families/IMUTag/family.mk`, so shared application
sources come from `embedded/tags/families/IMUTag/src`. The local
`src/hal_rtc_lld.c` and `src/power_modes.c` are inherited from the U375/BMM350
target shape.
