# IMUTagU3bmm350 Maintainer Notes

`IMUTagU3bmm350` is the STM32U375/Cortex-M33 build of the BMM350 IMUTag
variant. It keeps the U3 board, linker, RTCv3, and MX25U12843 external-flash
settings from `IMUTagU375`, while replacing the AK09940A magnetometer module
with the BMM350 module used by `IMUTagbmm350`.

The target clocks SYSCLK/HCLK from MSIRC0 divided by 4, giving a 24 MHz core
clock. This is faster than the original 12 MHz U3 bring-up clock and leaves
more headroom for 800 Hz acquisition without enabling an external clock or PLL.

## Hardware Shape

- Board files: generated `IMUTagU375` / `board-imutag-u375`.
- Flash: MX25U12843 SPI NOR flash path from `IMUTagU375`; long storage data
  phases use SPI1 DMA3 block transfers.
- IMU: LSM6DSV16X SPI binding and LPTIM2 trigger output from the IMUTag
  family. FIFO reads use SPI1 DMA3 for each 7-byte FIFO word transaction.
- Pressure: LPS22HH SPI binding from the IMUTag family.
- RTC and BMM350 bus: shared software I2C on the RTC pin pair.
- BMM350 INT/DRDY: PA4, exposed by the board as `LINE_IMU_TRG_TEST`.

`inc/custom.h` keeps `SWAP_I2C` disabled because this U375 board uses the
normal RTC SDA/SCL line order for the software-I2C descriptors. The target
otherwise keeps the U3 flash/RTC bring-up defines from `IMUTagU375`.

This target intentionally uses software I2C: the generated U375 board labels
PB6 as `RTC_SDA` and PB7 as `RTC_SCL`, while STM32 I2C1 hardware maps PB6 to
SCL and PB7 to SDA. Software I2C follows the board net labels and avoids that
hardware pin-function swap.

This target also enables `TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN` so the shared
software-I2C controller releases a stuck RTC/BMM350 bus before each session.
It uses `TAG_RTC_I2C_DELAY_CYCLES=7U`, giving the bit-banged RTC/BMM350 bus
some transition margin while keeping the measured clock near the earlier
112 kHz bring-up point.

This STM32U3 target wakes from Stop1 through the PA0/`LINE_WKUP1` EXTI event
path, matching the STM32L432 IMUTag model and avoiding PAL interrupt delivery
during detached acquisition.

`TAG_CONFIGURED_IMMEDIATE_START` also keeps an immediate host start from taking
the intermediate CONFIGURED standby path before collection sensors have been
initialized. This matters when the monitor detaches right after the start ACK.

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
