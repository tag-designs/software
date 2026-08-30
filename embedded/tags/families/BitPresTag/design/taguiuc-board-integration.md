# TagUIUC Board Integration Plan

## Scope

This note plans the firmware changes needed to integrate the **TagUIUC** board into
the BitPresTag family. TagUIUC differs from the baseline BitPresTag in two key
ways:

1. **Accelerometer**: Uses **ADXL367** instead of ADXL362, connected via
   **USART2 in 4-wire SPI mode**.
2. **Pressure Sensor**: Uses **BMP585** (software-compatible with BMP581)
   instead of LPS27, connected via **SPI1**, with an additional **LPS_RDY**
   interrupt line for data-ready and power-on-reset detection.

The TagUIUC board is treated as a new variant of the BitPresTag family, sharing
all common application logic while overriding sensor bus bindings and
interrupt handling.

## Existing BitPresTag Model

The current BitPresTag family uses:

- **ADXL362** on a dedicated SPI bus (SPI2), configured in wake-mode for
  activity detection.
- **LPS27** pressure sensor on a USART in SPI mode (USART1), polled during
  RTC wake intervals.
- Single-wire wake sources: ADXL362 for activity, LPS_RDY unavailable.

The LPS27 driver powers the sensor, starts a one-shot conversion, polls status,
reads the sample, and powers down before returning. This keeps the MCU awake
during conversion and assumes no pressure interrupt is available.

## TagUIUC Hardware Differences

| Component | BitPresTag (Baseline) | TagUIUC Variant |
|-----------|------------------------|--------------|
| Accelerometer | ADXL362 (SPI2) | ADXL367 (USART2, 4-wire SPI) |
| Pressure Sensor | LPS27 (USART1, SPI mode) | BMP585 (SPI1, with LPS_RDY) |
| Pressure Interrupt | None | LPS_RDY (EXTI) |

### ADXL367 on USART2

The ADXL367 is fully compatible with the existing
`embedded/tags/common/sensors/accel/src/ADXL367.c` driver. The driver uses
standard 4-wire SPI transactions (`tagSpiPolledSend`, `tagSpiPolledReceive`,
`tagSpiWrite`, `tagSpiRead`) and expects a `TagSpiDevice` descriptor.

USART2 must be configured in **4-wire SPI mode** with:
- CPOL = 0, CPHA = 0 (ADXL367 requires this)
- Slave mode (`USART_CR2_SLVEN = 1`)
- Software SS management (`USART_CR2_SSM = 1`)
- TX/RX enabled (`USART_CR1_TE = 1`, `USART_CR1_RE = 1`)

### BMP585 on SPI1 with LPS_RDY

The BMP585 is software-compatible with BMP581. The shared
`embedded/tags/common/sensors/pressure/src/bmp581.c` driver already supports
both chip IDs (`0x50` and `0x51`). Key API functions used:

- `bmp581_config_forced_device()` — configure forced-mode, leave powered.
- `bmp581_trigger_forced_device()` — start one conversion.
- `bmp581_data_ready_device()` — poll DRDY (or use interrupt).
- `bmp581_read_pressure_temp_powered_device()` — read sample without power-down.
- `bmp581_clear_interrupt_status_device()` — read/clear INT_STATUS.

The LPS_RDY line must be wired to an EXTI channel and configured as:
- Active-low (falling edge) for DRDY/POR
- Latched mode (default for BMP585)
- External pull-up (or internal if external is missing)

## Driver Availability

| Sensor | Driver Path | Notes |
|--------|-------------|-------|
| ADXL367 | `embedded/tags/common/sensors/accel/` | Already present; uses `TagAdxl367Device` descriptor |
| BMP585 | `embedded/tags/common/sensors/pressure/` | Uses `bmp581.c`; supports chip ID `0x51` |

No driver modifications are required. Only board-level binding and interrupt
handling need to be implemented.

## TagUIUC Firmware Integration Plan

### 1. Board CMake Integration

- Register `uiuc` as a new target in `embedded/CMakeLists.txt`.
- Create `embedded/boards/uiuc/CMakeLists.txt` based on BitPresTag.
- Add `uiuc` to `embedded/tags/*/BUILD_SOURCES.md`.

### 2. Board Hardware Configuration (`board.c`)

- Configure **USART2 in 4-wire SPI mode** for ADXL367:
  - Map SCK, MOSI, MISO, CS pins per TagUIUC schematic.
  - Set CPOL=0, CPHA=0, slave mode.
- Configure **SPI1** for BMP585:
  - Map SCK, MISO, MOSI, CS pins.
  - Set CPOL=0, CPHA=0, master mode.
- Wire **LPS_RDY** to EXTI (e.g., `GPIOC_PIN7` → `EXTI7`):
  - Configure as falling-edge, active-low.
  - Enable EXTI interrupt in NVIC.

### 3. Device Descriptors (`devices.c`)

```c
// Example TagUIUC device bindings (adjust pins per schematic)
static const TagSpiDevice uiuc_adxl367_spi = {
  .spi = &SPID2,             // USART2 in SPI mode
  .cs = GPIOC, .cs_pin = 12, // CS pin
  .power = &TagUIUC_POWER_SPI,
  .flags = TAG_SPI_FLAG_4WIRE
};

static const TagAdxl367Device uiuc_adxl367 = {
  .bus = TAG_BUS_SPI_CONST(&uiuc_adxl367_spi)
};

static const TagSpiDevice uiuc_bmp585_spi = {
  .spi = &SPID1,
  .cs = GPIOB, .cs_pin = 10,
  .power = &TagUIUC_POWER_SPI,
  .flags = TAG_SPI_FLAG_4WIRE
};

static const TagRegisterDevice uiuc_bmp585_reg = {
  .bus = TAG_BUS_SPI_CONST(&uiuc_bmp585_spi),
  .dummy_byte = 0xFF
};

static const TagPressureDevice uiuc_bmp585 = {
  .registers = &uiuc_bmp585_reg,
  .power = &TagUIUC_POWER_PRESSURE,
  .after_power_off = uiuc_pressure_after_power_off
};
```

### 4. Interrupt Handling (`exti.c`)

```c
// EXTI7_IRQHandler for LPS_RDY
void EXTI7_IRQHandler(void) {
  if (EXTI->PR & EXTI_PR_PR7) {
    EXTI->PR = EXTI_PR_PR7; // Clear pending
    tagPressureDeviceInterrupt(&uiuc_bmp585);
  }
}
```

### 5. Sensor Manager Integration

- Add `uiuc_adxl367` and `uiuc_bmp585` to sensor lists.
- Use `sensor_accel_adxl367` and `sensor_pressure_bmp581` in CMake source lists.
- Configure `uiuc_sensor_config.json` with appropriate sampling rates.

### 6. State Machine Updates

- **RTC Wake**: Power BMP585, trigger forced conversion, enable LPS_RDY wake.
- **LPS_RDY Wake**: Read/clear INT_STATUS, decode POR/DRDY, read sample, log.
- Preserve activity snapshot alignment between RTC and pressure samples.

## Host-Side Integration

### 1. Device Database (`host/libraries/tagcore/src/Device.cpp`)

```cpp
else if (board_id == "uiuc") {
  info.board_desc = "TagUIUC (ADXL367 + BMP585)";
  info.accelconstant = 0.00025f; // ADXL367 scale
  info.magconstant = 0.0f;      // No magnetometer
}
```

### 2. sensorViz UI

- Add TagUIUC to board selection dropdown.
- Ensure ADXL367 and BMP585 streams render correctly.

### 3. Log Schema

No changes needed:
- ADXL367 uses existing `Adxl362.accel_type = AdxlType_367`.
- BMP585 uses existing `PresTagLog` structure.

## Validation Sequence

1. **Chip ID Probe**: `RUN_LPS` test confirms BMP585 chip ID `0x51`.
2. **ADXL367 SPI Transaction**: Logic analyzer confirms 4-wire transactions on USART2.
3. **LPS_RDY Wake**: GPIO monitor confirms EXTI triggers on BMP585 DRDY.
4. **Full RUNNING Capture**: Verify RTC + LPS_RDY wake sequence produces aligned logs.
5. **Power Analysis**: Compare energy usage of powered-down vs. standby BMP585.

## Open Questions

- Which EXTI line is assigned to LPS_RDY on TagUIUC hardware?
- Is the LPS_RDY pull-up external or internal? (Affects EXTI configuration.)
- Should TagUIUC use a distinct firmware string (`taguiuc` vs `bitprestag`)?
- Is there a need for ADXL367-specific configuration (e.g., FIFO mode) beyond ADXL362?

## Related Design Notes

- [BMP581/BMP585 forced-mode pressure plan](bmp581-forced-mode.md): Driver and
  BitPresTag-family integration plan for interrupt-driven forced pressure sampling.
- [ADXL367 driver](embedded/tags/common/sensors/accel/inc/ADXL367.h): Descriptor-backed
  register driver with standard SPI transactions.
- [BMP581 driver](embedded/tags/common/sensors/pressure/inc/bmp581.h): Descriptor-backed
  pressure driver supporting BMP581/BMP585 chip IDs.
## Implementation Status

**Status**: *In Progress* — Board integration skeleton created. The following
files have been added to the repository:

| File | Purpose |
|------|---------|
| `embedded/tags/TagUIUC/CMakeLists.txt` | CMake target registration |
| `embedded/tags/TagUIUC/project.mk` | Build manifest (ADXL367, BMP585, flash_at25xe) |
| `embedded/tags/TagUIUC/src/devices.c` | TagUIUC-specific sensor descriptors |
| `embedded/tags/TagUIUC/cfg/` | ChibiOS configuration (copied from BitPresTag) |
| `embedded/boards/TagUIUC/CMakeLists.txt` | Board generation target |
| `embedded/tags/CMakeLists.txt` | Added TagUIUC subdirectory |
| `embedded/tags/families/BitPresTag/design/taguiuc-board-integration.md` | This design note |

The remaining work involves:
- Completing board pin assignments in `board-customizations.json`
- Wiring LPS_RDY interrupt to EXTI and implementing handler
- Host-side integration (device database, sensorViz UI)
- Validation testing

---
