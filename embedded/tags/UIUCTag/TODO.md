---
type: worklist
status: current
summary: Open UIUCTag items from the 2026-10-04 bench session -- a run that stored nothing, a self-test that fails, and the internal-ADC fix that came out of it.
---

# UIUCTag TODO

Opened 2026-10-04. All of these are from one session on the shared breakout
board, UUID `2036354B3032500800520028` -- the same physical unit the CompassTag
log calls `CompassTagAT25Breakout`. It is a development board, not production
UIUCTag hardware, so none of this is a qualification result.

- **A 20-minute run stored nothing.** On `fw-v0.5`, a run from 20:55:25 to
  21:18:36 ended with `external_pages=0` and `pState->pages == 0`, and the
  download contained only `info`, `schema_info`, `streams` and `states` -- no
  data tables at all. At a 300 s block period that run should have written
  four blocks. This is not the download hiding a partial block: `data_logAck()`
  trims trailing unwritten slots and returns what a partial block holds, and
  it refuses only when `index >= pState->pages`. Unexplained.
- **`tag-test` fails reproducibly on `RUN_ALL`.** Three consecutive attempts,
  each dying with `read debug register 0xe000edfc failed status=0x81`,
  `LIBUSB_ERROR_TIMEOUT` and `Tag returned an empty or invalid
  acknowledgement`. Not the transient link fault this rig shows from time to
  time. Whether this and the empty run share a cause is unknown; they were
  found together and should be investigated together.
- **Confirm the internal-ADC fix holds here.** This board is what exposed the
  `adcVDD()` defects: it reported `vdd=3.77-3.96` and `temp=92.5-111.4` while
  the rail was 2.4960 V and the room about 25 C. Both outputs come from one
  call and the temperature is computed from the voltage, so one unsettled
  VREFINT sample corrupted both. The same code reads correctly on BitTag and
  CompassTag, so the margin was board-dependent rather than absent. After the
  fix this board read 2.48-2.49 V and 23.9-25.3 C; that was two samples, and
  wants confirming over a longer run and against the BMP581's own temperature.
- **Find the shortest sampling time that still reads correctly.** 640.5 cycles
  was chosen to remove the clock dependency, not because it was measured to be
  necessary. The settling delay may be doing all the work. Sweep it on this
  board; the harness is `adc_sweep.sh` from the 2026-10-04 session.
- **Verify the stored-configuration write.** UIUCTag is one of the seven
  targets whose `writeStoredConfig()` ignores the result -- see
  [the proposal](../design/proposals/stored-config-write-is-unchecked.md).
  Worth ruling in or out as a cause of the empty run above, since a refused
  config write leaves the tag running something nobody chose.
- **No power qualification exists.** The plan is
  [`design/power-test-plan.md`](design/power-test-plan.md) and the log is
  [`design/power-results.md`](design/power-results.md), both written
  2026-10-04 and neither yet carrying a passing session. The resting currents
  measured on this breakout -- `IDLE` 0.1628 µA, `FINISHED` 0.1640 µA,
  `RUNNING` 0.2466 µA -- are recorded here only as orientation, not as a
  baseline: a breakout is not the hardware that flies.
