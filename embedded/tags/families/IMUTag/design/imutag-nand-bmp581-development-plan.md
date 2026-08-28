# IMUTagNandBmp581 Development Plan

## Purpose

`IMUTagNandBmp581` is the planned firmware model for the revised
IMUTagNand replacement board. It should remain host-compatible with the
existing IMUTag protocol and log tooling while selecting the revised hardware:
BMP581 pressure sensing and GD5F2GM7REYIGR SPI-NAND storage.

The firmware model name is new, but the public tag identifier is not:

```text
TagType: IMUTAG
Config:  embedded/proto-c/imutag-proto-c/default-config.json
Logs:    existing IMUTagRawLog / IMUTag SQLite import path
```

Keeping the `IMUTAG` identifier avoids host and stored-log churn for a board
that is a hardware replacement rather than a new product class.

## Reference Inputs

- Hardware reference schematic:
  `hardware/BoardDesigns/IMUTagNandBMP581-breakout/IMUTagNandBMP581-breakout.kicad_sch`
- Hardware reference PCB:
  `hardware/BoardDesigns/IMUTagNandBMP581-breakout/IMUTagNandBMP581-breakout.kicad_pcb`
- Existing firmware target:
  `embedded/tags/IMUTagNand/`
- Existing IMUTag family implementation:
  `embedded/tags/families/IMUTag/`
- Existing v2 generated-board source:
  `embedded/boards/IMUTagNandv2/`
- BMP581 datasheet:
  `hardware/BoardDesigns/libraries/datasheets/bst-bmp581-ds004.pdf`
- GD5F2GM7RE datasheet:
  `hardware/BoardDesigns/libraries/datasheets/DS_00819_GD5F2GM7RE_Rev1_3-3435814.pdf`
- Bosch BMP5 Sensor API:
  `https://github.com/boschsensortec/BMP5_SensorAPI`

Do not use similarly named older board directories as the reference for this
work. In particular, `IMUTagNandBreakcoutv2` is not the BMP581/GD5F2GM7
hardware reference.

## Hardware Delta

The corrected schematic uses these firmware-relevant signals:

| Function | Schematic net | Firmware line intent |
| --- | --- | --- |
| IMU interrupt/wake | `WKUP1` | Existing IMU FIFO wake input |
| IMU external trigger | `LSM_TRG` | LPTIM-driven trigger output |
| IMU chip select | `LSM_CS` | Existing LSM6DSV16X SPI CS |
| Shared IMU/NAND SCK | `AT25_SCK` | Existing IMU/flash SPI clock alias |
| Shared IMU/NAND MISO | `AT25_MISO` | Existing IMU/flash SPI MISO alias |
| Shared IMU/NAND MOSI | `AT25_MOSI` | Existing IMU/flash SPI MOSI alias |
| NAND chip select | `AT25_nCS` | GD5F2GM7RE SPI-NAND CS |
| NAND load switch enable | `FLASH_PWR` | Keep asserted for initial bring-up |
| BMP581 SCK | `LPS_CK` | Legacy pressure SPI clock name |
| BMP581 MOSI | `LPS_MOSI` | Legacy pressure SPI MOSI name |
| BMP581 MISO | `LPS_MISO` | Legacy pressure SPI MISO name |
| BMP581 chip select | `LPS_CS` | Pressure SPI CS |
| BMP581 interrupt | `LPS_DRDY` | Pressure data-ready input |
| BMM350 interrupt | `BMM_INT` | Existing BMM350 data-ready input |
| BMM350 / RTC I2C | `SCL`, `SDA` | Existing shared I2C pair |

The `LPS_*` pressure names are intentionally stale for this board because they
now connect to BMP581. Keep them for the first firmware pass to minimize board
file and family-code churn. A later cleanup can introduce neutral
`PRESSURE_*` aliases once bring-up is stable.

## Existing Contracts To Preserve

- The tag advertises `TagType::IMUTAG`.
- Host download, SQLite import, and SensorViz behavior continue to use the
  existing IMUTag raw-log path.
- IMU, pressure, and magnetometer auxiliary samples remain packed into the
  existing IMUTag superframe shape.
- Logged pressure remains hPa.
- The latest raw pressure-sensor temperature remains in the current IMUTag
  header/export representation. BMP581 native temperature must be converted
  into that representation rather than changing protobuf fields.
- NAND-backed sparse internal checkpoints keep the existing cadence and
  recovery model described in `internal-header-checkpoints.md`.

## Target Firmware Shape

Create a new tag target:

```text
embedded/tags/IMUTagNandBmp581/
```

The target should mostly mirror `embedded/tags/IMUTagNand/`:

- `CMakeLists.txt` adds `IMUTagNandBmp581` with the existing `imutag_proto`.
- `project.mk` includes the corrected v2 board and the shared IMUTag family.
- `inc/custom.h` carries only variant-specific identity, board aliases,
  feature switches, storage geometry, and bring-up overrides.
- Existing local U375 support files can be copied only where still required:
  `src/hal_rtc_lld.c`, `src/power_modes.c`, and any SPI workaround source
  that remains necessary for this hardware.

The new target should select these modules:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_gd5f2gm7re \
       sensor_pressure_bmp581 \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x
```

## Board File Work

`embedded/boards/IMUTagNandv2` currently exists but must become distinct from
the v1 board before the firmware target consumes it.

Required changes:

- Generate target name: `board-imutag-nand-v2`.
- `BOARD_TYPE`: `IMUTagNandv2`.
- JSON `board_id`: `IMUTagNandv2`.
- JSON `board_name`: update to identify the BMP581/GD5F2GM7 board revision.
- Add the board directory to `embedded/boards/CMakeLists.txt`.
- Add a board README or update `embedded/boards/README.md` so the v2 board is
  discoverable and associated with `IMUTagNandBmp581`.
- Fix generated line assumptions in the new target:
  - `IMUTAG_IMU_TRIGGER_LINE` should bind to `LINE_LSM_TRG`, not the v1
    `LINE_LMS_TRIG_2` spelling.
  - flash aliases should bind to the `AT25_*` net names emitted by the board.
  - pressure aliases can continue to bind to `LPS_*` line names for now.
  - flash load-switch enable should be represented as `LINE_FLASH_PWR`.

## BMP581 Pressure Driver Work

Add a shared pressure module rather than embedding BMP581 register sequencing
inside `families/IMUTag/src/sensors.c`.

Required files:

- `embedded/tags/common/modules/sensor_pressure_bmp581.mk`
- `embedded/tags/common/sensors/pressure/inc/bmp581.h`
- `embedded/tags/common/sensors/pressure/src/bmp581.c`
- `embedded/tags/common/sensors/pressure/src/bmp581_test.c`
- imported Bosch BMP5 Sensor API source/header files, with license preserved

Driver requirements:

- Wrap Bosch `struct bmp5_dev` callbacks around `TagRegisterDevice`.
- Use SPI mode 0/3-compatible register access on the existing pressure SPI bus.
- Perform the BMP581 dummy SPI read after power-up/reset before relying on
  returned register values.
- Validate chip ID `0x50`.
- Configure pressure and temperature measurement, ODR, OSR, data-ready source,
  and interrupt pin mode through the Bosch API where practical.
- Provide a descriptor-backed API matching the needs of IMUTag collection:
  - check identity;
  - configure continuous sampling;
  - optionally configure triggered/forced sampling;
  - test data-ready;
  - read one coherent pressure/temperature pair;
  - enter standby or deep standby.
- Convert BMP581 pressure from Pa to hPa for the superframe.
- Convert BMP581 temperature from degrees C to the existing raw centi-degree C
  representation used by IMUTag headers.

Initial collection policy should mirror the LPS22HH behavior: select a BMP581
normal-mode ODR high enough that each IMU superframe poll usually finds fresh
pressure data. FIFO use is a later optimization, not required for first
bring-up.

## GD5F2GM7RE Storage Work

Reuse the existing GD5F SPI-NAND command implementation and add a density
module for the 2 Gbit 1.8 V part.

Required file:

```text
embedded/tags/common/modules/flash_gd5f2gm7re.mk
```

Required compile-time geometry and identity:

```c
#define GD5F_ID_MANUFACTURER      0xC8U
#define GD5F_ID_DEVICE            0x82U
#define GD5F_PAGE_SIZE            2048UL
#define GD5F_SPARE_SIZE           128UL
#define GD5F_PAGES_PER_BLOCK      64UL
#define GD5F_PHYSICAL_BLOCK_COUNT 2048UL
#define GD5F_MIN_VALID_BLOCK_COUNT 2008UL
```

The datasheet states that the GD5F2GM7RE family is 2 Gbit, has 2048 physical
blocks, uses 2048-byte data pages, and identifies `GD5F2GM7RExxG` as
manufacturer `0xC8` and device `0x82`.

Capacity must be exposed through the logical bad-block-free map, not by the
nominal raw density. The existing IMUTag datalog path uses:

```text
externalFlashSize() = tagStorageSectorSize(TAG_EXTERNAL_FLASH)
                    * tagStorageSectorCount(TAG_EXTERNAL_FLASH)
```

For GD5F NAND that should resolve to:

```text
GD5F_BLOCK_SIZE * GD5F_LOGICAL_BLOCK_COUNT
```

Using `GD5F_MIN_VALID_BLOCK_COUNT` as the first logical block count gives
`2008 * 64 * 2048 = 263192576` usable bytes before filesystem/log overhead.

## Flash Load Switch Policy

The schematic routes `FLASH_PWR` to the load-switch enable for NAND power.
The default board configuration enables it, and early firmware can treat the
flash rail as always powered.

Initial implementation:

- Drive `LINE_FLASH_PWR` high as part of normal board/device initialization.
- Keep NAND command bring-up focused on ID, map provisioning, erase, program,
  read, and download.
- If the generic SPI descriptor `.pwr = LINE_FLASH_PWR` path causes sequencing
  churn, leave `.pwr = TAG_NO_LINE` for the first bring-up and keep the board
  line asserted explicitly.

Later low-power implementation:

- Teach the GD5F driver about Deep Power-Down command `0xB9`.
- Teach wake/sleep about Release from Deep Power-Down command `0xAB`.
- Coordinate deep power-down with the load switch so feature-register state,
  ECC enable, and block unlock are restored after release or power cycling.

## IMUTag Family Integration

The current IMUTag family code directly includes and calls LPS22HH APIs.
For first bring-up, small compile-time branches are acceptable:

- include `bmp581.h` when `TAG_SENSOR_PRESSURE_BMP581` is selected;
- add a BMP581 pressure-rate selector;
- branch pressure configure/read/idle/self-test paths;
- preserve existing LPS22HH behavior for `IMUTagNand`.

Once BMP581 is proven, consider introducing a `TagPressureOps` table so IMUTag
collection calls neutral pressure operations and future pressure changes stay
inside driver modules.

## Bring-Up Sequence

1. Build board generation for `IMUTagNandv2`.
2. Build `IMUTagNandBmp581`.
3. Smoke-test GPIO idle states:
   - all chip-select lines idle high;
   - shared SPI pins do not fight unpowered devices;
   - `FLASH_PWR` asserted for the initial all-on policy;
   - `LSM_TRG` toggles at the expected LPTIM-derived rate.
4. Run BMP581 self-test:
   - dummy SPI read;
   - chip ID `0x50`;
   - one pressure/temperature sample;
   - data-ready behavior.
5. Run GD5F2 self-test:
   - reset;
   - read ID `0xC8 0x82`;
   - block unlock;
   - ECC enable;
   - factory bad-block scan.
6. Provision and validate the NAND logical block map.
7. Exercise erase/program/read on one logical NAND page and one full block.
8. Run IMU-triggered collection with pressure and magnetometer auxiliary
   samples enabled.
9. Download the log and verify existing host tools import it as `IMUTAG`.
10. Measure idle, configured, collecting, and post-collection current before
    enabling GD5F2 deep power-down or load-switch cycling.

## Verification Gates

- `cmake --build <build-dir> --target board-imutag-nand-v2`
- `cmake --build <build-dir> --target IMUTagNandBmp581`
- `tag-test` or monitor self-test for RTC, flash, IMU, pressure, and BMM350.
- NAND map absent case refuses collection with a clear configuration error.
- NAND map provisioned case allows configuration and collection.
- Downloaded log reports `tag_type = IMUTAG`.
- Host SQLite output contains IMU, pressure, magnetometer, and metadata streams
  without schema changes.

## Known Risks

- The v2 board source currently needs cleanup before it is safe to consume as a
  distinct generated board.
- BMP581 SPI protocol selection requires a dummy SPI read after power-up or
  reset; missing that can make the first real transaction look broken.
- BMP581 temperature scaling differs from LPS22HH and must be converted before
  writing IMUTag header raw temperature.
- GD5F2GM7RE doubles the physical block count from the current 1 Gbit target;
  capacity reporting, map provisioning, and map validation must agree.
- The GD5F logical map lives in one STM32U375 flash page. A 2048-entry
  `uint16_t` map is exactly 4096 bytes, so alignment and linker assertions must
  be watched carefully.
- Deep power-down and load-switch cycling can invalidate assumed feature
  register state. Keep them out of the first bring-up path.

## Non-Goals For First Pass

- No new protobuf tag type.
- No host-visible log schema change.
- No BMP581 FIFO logging.
- No automatic NAND load-switch cycling during collection or download.
- No pressure net rename from `LPS_*` to neutral names until after bring-up.
- No change to IMUTag sparse checkpoint cadence.

## Open Questions

- Should `IMUTagNand` eventually move to the v2 board file, or should
  `IMUTagNandBmp581` remain a separate permanent firmware target?
- Should BMP581 continuous pressure run in normal mode or forced mode paced by
  IMU superframes once power measurements are available?
- Should GD5F2 deep power-down be part of idle/configured states only, or also
  between collection bursts if the flash load switch remains always-on?
- Should the host UI display the firmware string as the sole hardware
  distinction, or should a separate board/model metadata field be added later?
