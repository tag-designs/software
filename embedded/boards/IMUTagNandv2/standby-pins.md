# IMUTagNandv2 Standby Pin Bias

IMUTagNandv2 uses an STM32U375. In Standby mode the normal GPIO mode,
output-level, and alternate-function configuration is released, so pins that
must remain at a known level need to be biased through the STM32U3 PWR
standby pull-up and pull-down registers. The `Standby` entries in
`cfg/board-customizations.json` generate those PWR masks.

The BMP581 pressure header follows the breakout silkscreen: PA9 is
`LPS_DRDY`, and PA10 is `LPS_CS`.

These pulls are weak standby biases. They are not retained GPIO output drive.
Any net that must be held strongly through Standby needs an external resistor
or a lower-power mode that preserves GPIO output state.

The GD5F SPI-NAND flash follows the same standby rule as the other SPI
peripherals: chip-select is biased high to keep the part deselected, clock and
MOSI are biased low, and MISO is weakly biased because the flash output driver
tri-states while deselected. On this board the flash shares the
`AT25_SCK`, `AT25_MISO`, and `AT25_MOSI` nets with the LSM6DSV.

## Bias Policy

| Signal | Standby bias | Reason |
| --- | --- | --- |
| `AT25_nCS` | Pull up | Keeps the GD5F SPI-NAND deselected while GPIO output drive is released. |
| `LSM_CS` | Pull up | Keeps the LSM6DSV deselected. The sensor has a CS pull-up, but the board still biases the MCU-side net. |
| `LPS_CS` | Pull up | Keeps the BMP581 deselected. The sensor has a CS pull-up, but the board still biases the MCU-side net. |
| `AT25_SCK` | Pull down | Shared GD5F/LSM6DSV SPI clock. SPI clock inputs must not float in standby. |
| `LPS_SCK` | Pull down | BMP581 SPI clock input must not float in standby. |
| `AT25_MOSI` | Pull down | Shared GD5F/LSM6DSV SPI input/data net can float when the interface is idle. |
| `LPS_MOSI` | Pull down | BMP581 SPI input/data net can float when the sensor interface is idle. |
| `AT25_MISO` | Pull down | GD5F and LSM6DSV MISO drivers tri-state when deselected. The MCU must weakly bias the input buffer in standby. |
| `LPS_MISO` | Pull down | BMP581 SDO/MISO tri-states when deselected or powered down. |
| `WKUP1` | None | LSM6DSV interrupt output can be high-Z, but this net is left unbiased while measuring whether interrupt pulldowns fight latched active-high outputs. |
| `LSM_TRG` | None | LSM6DSV external ODR trigger on PB4; left unbiased when the trigger is disabled for standby. |
| `LPS_DRDY` | None | BMP581 data-ready output can be high-Z, but is left unbiased while checking for pull conflicts. |
| `BMM_INT` | None | BMM350 interrupt is active-high in firmware; left unbiased while checking for pull conflicts. |
| `SDA` | Pull up | Shared RTC/BMM350 software-I2C data line must idle high. |
| `SCL` | Pull up | Shared RTC/BMM350 software-I2C clock line must idle high. |
| `LED1` | Pull down | Test output should not float or source current when GPIO output drive is released. |
| `testpin` | Pull down | Test output should not float when GPIO output drive is released. |

Pins without a listed standby bias are left to board-level external circuitry
or are not expected to connect to a powered CMOS input/output that can float
in Standby.


The debug pins are in AF pull-up/pull-down after reset:
- PA15: JTDI in pull-up
- PA14: JTCK/SWCLK in pull-down
- PA13: JTMS/SWDIO in pull-up
- PB4: NJTRST in pull-up
- PB3: JTDO/TRACESWO in floating state no pull-up/pull-down

## FLASH_PWR (PA8) across a reset

On the breakout, PA8 (`FLASH_PWR`) enables the NAND's TPS22916 load switch,
active high. The final tag has no switch.
- The firmware drives PA8 high from its board init (`board.h`: output, ODR
  high), and the Standby pull-up (`PULLUPA` bit 8) holds it there in Standby.
- **Neither survives a reset.** A system reset returns PA8 to analog and
  clears `PWR_APCR` and `PWR_PUCRA` (read as 0 under the attach's reset,
  2026-10-02).
- So from NRST until code drives PA8 again, the switch enable is held only by
  whatever is on the PCB. On the breakout, after an SWD attach the NAND then
  runs on residual charge and parasitic power through its IO pins for a few
  tens of milliseconds, and then drops off the bus.
- The firmware never notices, because it drives PA8 high at boot. Code that
  runs instead of the firmware must do the same. The SRAM loader
  (`embedded/loaders/GD5F2GM7RE_IMUTagNandv2`) drives PA8 high and waits 5 ms
  before using the NAND. That is harmless on the final tag.
- A hardware pull-up on the switch enable would keep the NAND powered across a
  reset. It was judged not worth a PCB change on a board the final tag does
  not share.
