# tag-test

`tag-test` is a developer/support tool that reads tag information, checks and
sets the RTC, prints state history, and runs tag self-tests when the tag is
idle. Use it during bench bring-up, troubleshooting, or before field deployment
when you want a quick hardware readiness check from the monitor interface.

## Usage

```sh
tag-test [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-t`, `--test` | `TestReq` | Runs one requested self-test by enum name or number; default is `RUN_ALL`. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

- Connect a compatible tag/base interface over USB.
- Run self-tests only when the tag is `IDLE`; `tag-test` exits before running
  the requested test if the tag reports another state.
- Download or preserve any important data before troubleshooting a tag with
  repeated tests. The command sets the RTC before and after testing, and some
  device tests reset or reinitialize the device they check.

## Examples

Run the full self-test list for the connected tag:

```sh
tag-test
```

Run all tests with debug messages enabled:

```sh
tag-test --debug
```

Run only the external flash test:

```sh
tag-test --test RUN_EXT_FLASH --debug
```

## Output

The command prints tag identity fields, firmware build information, flash
sizes, repository hash, UUID, voltage, RTC drift before and after setting the
clock, state-history entries, and the final `TestResult`.

`RUN_ALL` runs the tests supported by the connected firmware in that firmware's
table order and reports the first failing result. Use the per-tag tables below
when selecting a single `--test` value. Some request names are historical
protocol names; the explanation column names the device actually tested.

## BitTag And BitTag Legacy

| Test request | What it checks |
| --- | --- |
| `RUN_ADXL362` | Checks the ADXL362 accelerometer identity registers and verifies its built-in self-test response. |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |

## BitTagNG

| Test request | What it checks |
| --- | --- |
| `RUN_ADXL362` | Uses the legacy accelerometer request name to check ADXL367 identity and sample-conversion readiness. |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the AT25-series external flash and verifies that it returns a valid chip identity. |

## PresTag And PresTagRaw

| Test request | What it checks |
| --- | --- |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the AT25-series external flash and verifies that it returns a valid chip identity. |
| `RUN_LPS` | Checks that the LPS27 pressure sensor responds and can produce pressure/temperature samples. |

## BitPresTag

| Test request | What it checks |
| --- | --- |
| `RUN_ADXL362` | Checks the ADXL362 accelerometer identity registers and verifies its built-in self-test response. |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the AT25-series external flash and verifies that it returns a valid chip identity. |
| `RUN_LPS` | Checks that the LPS27 pressure sensor responds and can produce pressure/temperature samples. |

## BitPresTagMX25R

| Test request | What it checks |
| --- | --- |
| `RUN_ADXL362` | Checks the ADXL362 accelerometer identity registers and verifies its built-in self-test response. |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the MX25R external flash and verifies that it returns a valid chip identity. |
| `RUN_LPS` | Checks that the LPS27 pressure sensor responds and can produce pressure/temperature samples. |

## CompassTag, CompassTagAT25, And CompassTagAT25Breakout

| Test request | What it checks |
| --- | --- |
| `RUN_MMC5633` | Uses the legacy magnetometer request name to check that the AK09940A magnetometer identity registers are valid. |
| `RUN_AIS2` | Uses the legacy accelerometer request name to check that the LIS2DU12 accelerometer identity register is valid. |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the configured external flash and verifies that it returns a valid chip identity. |

CompassTag uses MX25R external flash. CompassTagAT25 and
CompassTagAT25Breakout use AT25-series external flash.

## IMUTagNand

| Test request | What it checks |
| --- | --- |
| `RUN_RTC` | Initializes the RTC, reads the date/time, and checks that the STM32 RTC divider configuration matches the external RTC source. |
| `RUN_EXT_FLASH` | Wakes the GD5F SPI-NAND flash, provisions or validates the logical map when needed, logs factory bad blocks, and verifies chip identity. |
| `RUN_AIS2` | Uses the legacy accelerometer request name to run the LSM6DSV16X accelerometer self-test. |
| `RUN_LPS` | Checks that the LPS22HH pressure sensor identity is valid. |
| `RUN_MMC5633` | Uses the legacy magnetometer request name to check BMM350 identity, reset behavior, and compensation-data readout. |

Some IMUTag-family firmware can also expose `RUN_MX25U12843` when built for
MX25U12843 flash. The active IMUTagNand target uses GD5F SPI-NAND, so
`RUN_EXT_FLASH` is the flash test to run for that target.

## Troubleshooting

- `Attach failed`: the USB interface was found but could not be opened.
- `State not idle`: the tag is not in `IDLE`, so `tag-test` did not run the
  requested self-test.
- `Invalid test`: the `--test` value was not a valid `TestReq` enum name or
  number.
- A device-specific failure such as `RTC_FAILED`, `ADXL362_FAILED`,
  `LPS27_FAILED`, `LPS22_FAILED`, `MX25R_FAILED`, `AK09940A_FAILED`, or
  `AIS2_FAILED` identifies the first self-test that failed during `RUN_ALL`.
