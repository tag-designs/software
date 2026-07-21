# IMUTagU3bmm350 Maintainer Notes

`IMUTagU3bmm350` is the STM32U375/Cortex-M33 build of the BMM350 IMUTag
variant. It keeps the U3 board, linker, RTCv3, and MX25U12843 external-flash
settings from `IMUTagU375`, while replacing the AK09940A magnetometer module
with the BMM350 module used by `IMUTagbmm350`.

## Hardware Shape

- Board files: generated `IMUTagU375` / `board-imutag-u375`.
- Flash: MX25U12843 SPI NOR flash path from `IMUTagU375`.
- IMU: LSM6DSV16X SPI binding and LPTIM2 trigger output from the IMUTag
  family.
- Pressure: LPS22HH SPI binding from the IMUTag family.
- RTC and BMM350 bus: shared software I2C on the RTC pin pair.
- BMM350 INT/DRDY: PA4, exposed by the board as `LINE_IMU_TRG_TEST`.

`inc/custom.h` keeps `SWAP_I2C` disabled because this U375 board uses the
normal RTC SDA/SCL line order. The target otherwise keeps the U3 flash/RTC
bring-up defines from `IMUTagU375`.

This target also enables `TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN` so the shared
software-I2C controller releases a stuck RTC/BMM350 bus before each session.
It uses the U3 software-I2C default `TAG_RTC_I2C_DELAY_CYCLES=64U` rather than
the faster `IMUTagU375` bring-up value.

## Build Membership

`project.mk` selects:

```make
rtc_rv3028
flash_mx25u12843
sensor_pressure_lps22hh
sensor_mag_bmm350
sensor_imu_lsm6dsv16x
```

The target includes `../families/IMUTag/family.mk`, so shared application
sources come from `embedded/tags/families/IMUTag/src`. The local
`src/hal_rtc_lld.c` is the U3 RTCv3 ChibiOS shim copied from `IMUTagU375`.
