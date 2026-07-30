# IMUTagNandv1 Standby Pin Bias

IMUTagNandv1 uses an STM32U375. In Standby mode the normal GPIO mode,
output-level, and alternate-function configuration is released, so pins that
must remain at a known level need to be biased through the STM32U3 PWR
standby pull-up and pull-down registers. The `Standby` entries in
`cfg/board-customizations.json` generate those PWR masks.

These pulls are weak standby biases. They are not retained GPIO output drive.
Any net that must be held strongly through Standby needs an external resistor
or a lower-power mode that preserves GPIO output state.

The GD5F SPI-NAND flash follows the same standby rule as the other SPI
peripherals: chip-select is biased high to keep the part deselected, clock and
MOSI are biased low, and MISO is weakly biased because the flash output driver
tri-states while deselected. On this board the flash shares the
`LSM_FLASH_SCK`, `LSM_FLASH_MISO`, and `LSM_FLASH_MOSI` nets with the LSM6DSV.

## Bias Policy

| Signal | Standby bias | Reason |
| --- | --- | --- |
| `FLASH_nCS` | Pull up | Keeps the GD5F SPI-NAND deselected while GPIO output drive is released. |
| `LSM_CS` | Pull up | Keeps the LSM6DSV deselected. The sensor has a CS pull-up, but the board still biases the MCU-side net. |
| `LPS_CS` | Pull up | Keeps the LPS22HH deselected. The sensor has a CS pull-up, but the board still biases the MCU-side net. |
| `LSM_FLASH_SCK` | Pull down | Shared GD5F/LSM6DSV SPI clock. SPI clock inputs must not float in standby. |
| `LPS_SCK` | Pull down | LPS22HH SPI clock input must not float in standby. |
| `LSM_FLASH_MOSI` | Pull down | Shared GD5F/LSM6DSV SPI input/data net can float when the interface is idle. |
| `LPS_MOSI` | Pull down | LPS22HH SPI input/data net can float when the sensor interface is idle. |
| `LSM_FLASH_MISO` | Pull down | GD5F and LSM6DSV MISO drivers tri-state when deselected. The MCU must weakly bias the input buffer in standby. |
| `LPS_MISO` | Pull down | LPS22HH SDO/MISO tri-states when deselected or powered down. |
| `WKUP1` | None | LSM6DSV interrupt output can be high-Z, but this net is left unbiased while measuring whether interrupt pulldowns fight latched active-high outputs. |
| `LSM_TRG` | None | LSM6DSV trigger/interrupt net can be high-Z, but is left unbiased during the interrupt-line standby-current experiment. |
| `LMS_TRIG_2` | None | Alternate LSM6DSV trigger candidate; left unbiased during the interrupt-line standby-current experiment. |
| `LPS_DRDY` | None | LPS22HH data-ready output can be high-Z, but is left unbiased while checking for pull conflicts. |
| `BMM_INT` | None | BMM350 interrupt is active-high in firmware; left unbiased while checking for pull conflicts. |
| `SDA` | Pull up | Shared RTC/BMM350 software-I2C data line must idle high. |
| `SCL` | Pull up | Shared RTC/BMM350 software-I2C clock line must idle high. |
| `LED1` | Pull down | Test output should not float or source current when GPIO output drive is released. |
| `testpin` | Pull down | Test output should not float when GPIO output drive is released. |

Pins without a listed standby bias are left to board-level external circuitry
or are not expected to connect to a powered CMOS input/output that can float
in Standby.


The debug pins are in AF pull-up/pull-down after reset:
• PA15: JTDI in pull-up
• PA14: JTCK/SWCLK in pull-down
• PA13: JTMS/SWDIO in pull-up
• PB4: NJTRST in pull-up
• PB3: JTDO/TRACESWO in floating state no pull-up/pull-down