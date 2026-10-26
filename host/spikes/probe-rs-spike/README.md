---
type: readme
status: current
summary: A probe-rs experiment against the tag bases - what each subcommand does and where the results are written up.
---

# probe-rs spike

An experiment, not a tool: it checks whether probe-rs 0.32 can drive the tag
bases and what debug state it leaves on a tag. Results are in
[`docs/investigations/2026-10-probe-rs-evaluation.md`](../../../docs/investigations/2026-10-probe-rs-evaluation.md).

```sh
cargo run -- list
cargo run -- raw [--clear]
cargo run -- session --chip STM32U375RG --sequence tag|st
RUST_LOG=probe_rs=trace cargo run -- raw   # probe-rs's own trace, to stderr
```

- `list`: the probes probe-rs finds.
- `raw`: holds nRST, attaches without a session, reads DPIDR and CTRL/STAT,
  the chip ID, UID, DHCSR, DEMCR and DBGMCU_CR, then releases nRST. With
  `--clear` it also writes those three registers back to their power-on
  values.
- `program --chip C --elf E [--dry-run]`: attaches under reset, erases only the
  sectors the image covers, programs and verifies it, resets so it runs, then
  runs `raw`.
- `session`: attaches under reset with probe-rs's built-in STM32 sequence
  (`st`) or the ARM default (`tag`, which writes nothing to DBGMCU), reads the
  halted core, drops the session, then runs `raw` to show what was left.

Every attach resets the tag, as with every tool in this tree. Quit qtmonitor,
qtprogram and anything else holding the base first.
