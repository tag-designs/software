---
type: readme
status: current
summary: tagprobe - programming, connect checks and flash read-back for tags through a base, on probe-rs, with a field-safe ST-LINK driver for bases whose firmware predates the identify command.
---

# tagprobe (Rust)

`tagprobe` wraps [probe-rs](https://probe.rs) 0.32 for the jobs the host tools
used to give STM32_Programmer_CLI:

- listing bases;
- the connect check (device ID, part, target voltage);
- programming an ELF with the right erase;
- reading flash back.

The choice behind it is in the
[probe-rs evaluation](../../../docs/investigations/2026-10-probe-rs-evaluation.md).
How it works, and the rules callers must keep, are in
[the design](design/architecture.md).

## Two drivers

Every base presents as an ST-LINK V2 (`0483:3748`).

- **`Driver::Stock`**: probe-rs's own ST-LINK driver. It works only with base
  firmware that has the ST-LINK fixes. Such firmware answers the `F6 99`
  identify command, so `identify()` returning a string selects it.
- **`Driver::FieldSafe`** (`src/fieldsafe.rs`): for bases in the field that
  cannot be reflashed. It avoids the commands their firmware gets wrong:
  - stale `READ_DAP_REG` replies;
  - 1-byte `READMEM_8BIT` replies;
  - `C_DEBUGEN` set on every attach.

`Driver::for_identity(identify(&base).as_deref())` picks between them.

## API

| Item | Does |
| --- | --- |
| `list()` | Bases on USB. |
| `identify(&base)` | The base's identity string, or `None` for older firmware. |
| `connect_check(&base)` | `Connect { device_id, device_name, voltage }`, attaching under reset. |
| `chip_for(device_id, flash_bytes)` | The probe-rs target: `STM32L432KC` or `STM32U375RG`. |
| `program(&base, driver, chip, elf, erase, on_progress)` | Erase (`ImagePages`, `Range`, `All`), program and verify in one session. Then clear `FLASH_SR.PEMPTY` on an L4 if it is set, and start the image from its vector table. |
| `read_memory(&base, driver, chip, addr, len)` | Read target memory under reset. |

Every call attaches **under reset**, so it ends whatever the tag was doing.
That is right for programming, and wrong for observing a running tag.

## Tests

`cargo test` checks the chip and driver selection without hardware. The
hardware results, on both base generations and both parts, are in the
evaluation document.
