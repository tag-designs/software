# Pin requirements

The stm32 parts are somewhat complex because they dynamically select between SPI and I2C.
In particular, they need pullups on MISO which is unusual

## LSM6DSV16x

- SDO/ACCEL_MISO         [Input without pullup] pulldown
- SDA/ACCEL_MAG_MOSI     [Input without pullup] pulldown
- CS/ACCEL_CS            [Input with pullup]    none
- SCL/ACCEL_LPS_MAG_SCK  [Input without pullup] pulldown
- INT1/WKUP1             [Active low push/pull] none
- INT2/INT_TRG           [Active low push/pull] none

## LPS22HH

- SDO/LPS_MAG_MISO       [Input without pullup]  pulldown
- SDA/LPS_MOSI           [Input without pullup]  pulldown
- SCL/ACCEL_LPS_MAG_SCK  [Input without pullup]  pulldown
- CS/MAG_CS              [Input without pullup]  pullup
- INT_DRDY/LPS_DRDY      [Active high push/pull] none

## AK09940A

- SCL/ACCEL_LPS_MAG_SCK  [Input without pullup] pulldown
- SO/LPS_MAG_MISO        [Input without pullup] pulldown
- SDA/ACCEL_MAG_MOSI     [High impedance] pulldown
- CS/MAG_CS              [Input without pullup] pullup
- DRDY/MAG_DRDY          [Output low] none