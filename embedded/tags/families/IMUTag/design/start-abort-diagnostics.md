# IMUTag Start Aborts: Evidence and a Proposal for Persistent Failure Detail

Status: open. Failure characterised and localised; the diagnostic described in
[Proposal](#proposal-a-detail-word-in-the-state-marker) is not implemented.

## The failure

Roughly one start attempt in three aborts immediately. It is not rate-specific
and not deterministic. Across two full power sweeps of five rate points each,
on an IMUTagNandBmp581 daughter card on the LDO breakout:

| Sweep | 100 Hz | 200 Hz | 400 Hz | 800 Hz | 1600 Hz |
| --- | --- | --- | --- | --- | --- |
| first | **abort** | pass | **abort** | pass | pass |
| second | pass | pass | pass | **abort** | not reached |

Three failures in nine attempts. Every failure immediately followed a
reset that erased external storage, but so did every success, so the erase is
at most a contributing condition and not the trigger.

## What the tag reports

Captured with `STOP_ON_FAILURE=1 power_sweep_imutag.sh`, which dumps state
before anything touches the tag — necessary because the next point's reset
erases both the marker log and the stored configuration:

```
Current state: ABORTED
  [0] CONFIGURED  reason=EVENT_STARTCMD  19:44:30  internal_pages=0 external_pages=0
  [1] RUNNING     reason=EVENT_STARTTIM  19:44:30  internal_pages=0 external_pages=0
  [2] ABORTED     reason=EVENT_UNKNOWN   19:44:30  internal_pages=0 external_pages=0
stored config: lsm6 { odr: S800 ... }
```

All three transitions land in the same second, and nothing was written.

## What that rules out

CONFIGURED and RUNNING are both recorded *before* the failure becomes
detectable, so their presence says nothing about progress:
`Configured(T_INIT)` calls `recordState()` before `writeStoredConfig()`, and
`Running(T_INIT)` calls `recordState()` before
`restartDataCollectionClock()`. A failed attempt therefore looks like a healthy
run until the abort.

The stored ODR reads back as `S800`, matching the request. That exonerates the
configuration path, including the previously suspected mechanism recorded in
`writeStoredConfig()` — flash programming can only clear bits, so a skipped
erase yields the bitwise AND of old and new, and `S400` over `S100` once
produced `0x000`. That is a real defect and worth keeping in mind, but it is
not this one: the configuration is correct and `get_lsm_config()` cannot be
failing on it.

`Running(T_INIT)` has exactly one other failure path,
`restartDataCollectionClock()`, whose only failure is `initDataCollection()`
returning false. In `sensors.c` that function returns false from three places:

1. `get_lsm_config()` — excluded by the stored configuration above.
2. `configure_mag_collection(true)` — BMM350 magnetometer.
3. `bmp581_config_continuous_device()` — BMP581 pressure sensor.

So the abort is an intermittent auxiliary-sensor initialisation failure, not
storage, not the configuration, and not the IMU.

## Why it is opaque

Three separate reasons, each independently worth fixing:

- `initDataCollection()` sets `ok = false` on either sensor failure and then
  **continues**, configuring the IMU FIFO and watermark before returning false.
  The tag aborts with sensors left half configured, and the two failures are
  indistinguishable from outside.
- Every diagnostic in the path is a `debug_log_printf()`. That module is
  excluded from shipped images because it prevents standby, so in practice
  none of these messages exist.
- `Aborted(T_INIT, State_EVENT_UNKNOWN)` is the only thing that reaches flash,
  and `EVENT_UNKNOWN` carries no information. The marker log records that the
  tag gave up, never why.

The net effect is a field failure mode that consumes a deployment and leaves
behind no evidence beyond "aborted".

## Proposal: a detail word in the state marker

Carry a diagnostic word in the existing marker, written to internal flash by
the `recordState()` call that already happens on the abort.

### This is available on STM32U3 targets only

The free space exists because the U3 record is padded out to the 128-bit flash
programming row, and it exists *only* there:

| Target | `sizeof(t_StateMarker)` | Alignment | Slack |
| --- | ---: | ---: | --- |
| STM32U3 (`IMUTagNand`, `IMUTagNandBmp581`) | 32 | 16 | 8 bytes of explicit `flash_padding` |
| STM32L4 (`UIUCTag`, `PresTag`, `CompassTag`, ...) | 24 | 8 | **none** |

The L4 record is 24 bytes of real fields, and 24 is already a multiple of the
L4 flash doubleword, so there is no padding to reclaim — not merely no declared
padding, but no slack at all. Adding a 4-byte field there rounds the record to
32 bytes under `aligned(8)`, which would cost a third of the marker-log
capacity in a fixed-size region, move every subsequent record, and make every
existing log on a deployed 432 tag unreadable. That is not acceptable, and the
432 tags are in any case out of scope for this work.

So the field is guarded by the same condition as the padding it replaces, and
non-U3 targets are bit-identical. The tag exhibiting the failure is a U375
board, so this covers the case at hand; if L4 detail is ever needed it wants
its own mechanism, such as a single dedicated record outside `sEpoch`, and is
deliberately out of scope here.

### The change

`t_StateMarker` currently reserves the row padding as one field:

```c
  State_Event reason;      ///< Event that caused the transition.
#if IMUTAG_STM32U3_FLASH
  uint64_t flash_padding;  ///< Padding required for STM32U3 flash rows.
#endif
} t_StateMarker __attribute__((aligned(16)));
```

Split it, under the existing guard:

```c
#if IMUTAG_STM32U3_FLASH
  uint32_t detail;         ///< Reason-specific diagnostic; 0 when unused.
  uint32_t flash_padding;  ///< Remaining padding for the 128-bit flash row.
#endif
```

The record stays 32 bytes, so `static_assert(sizeof(t_StateMarker) == 32)`
still holds and the log capacity, addresses and layout are all unchanged. The
same split is needed in both the family header and the common one, which
declare the record identically.

`recordState()` already `bzero()`s the marker before programming, so `detail`
is 0 in every existing marker and 0 means "no detail" for free — U3 logs
written by current firmware stay readable, and new firmware reading an old log
sees no spurious detail.

### Getting the bits to `recordState()`

`Aborted()` lives in shared `state_machine.c` and must not learn about
magnetometers. Use a weak hook that shared code calls and a family overrides:

```c
/* core/src/persistent.c */
uint32_t __attribute__((weak)) tagStateMarkerDetail(void)
{
  return 0U;
}
```

`recordState()` sets `marker.detail = tagStateMarkerDetail();`, under the same
`TAG_STM32U3_FLASH` guard as the field. The IMUTag family defines the strong
version in `sensors.c`, returning latched bits from the most recent
`initDataCollection()` attempt. Other families link the weak default, and on
non-U3 targets neither the field nor the call exists, so those images are
unchanged.

### Suggested bit layout

| Bits | Meaning |
| --- | --- |
| 0 | `get_lsm_config()` rejected the stored configuration |
| 1 | `configure_mag_collection()` failed (BMM350) |
| 2 | `bmp581_config_continuous_device()` returned nonzero |
| 3 | pressure sensor is LPS22HH rather than BMP581 |
| 15:8 | driver return code from the first failing device |
| 31:16 | reserved |

Bits rather than an enum, because both sensors can fail in the same attempt and
knowing that is the difference between "this sensor is marginal" and "the bus
or a shared supply is marginal".

### Reporting it

The transport needs one additive field. `State` currently has fields 1 and 2:

```proto
message State {
  Status status = 1;
  Event transition_reason = 2;
  uint32 transition_detail = 3;  // reason-specific; 0 when unused
}
```

Field 3 is free and proto3 omits it when zero, so nothing changes on the wire
for markers without detail — including every marker from an L4 tag, which
never sets it. `tag-info` decodes it beside the reason, and `sqlitelog` can
carry it in the `states` table so a downloaded log preserves it. The host side
is therefore uniform across targets: a tag with no detail to give simply
reports none, which is indistinguishable from a transition that had none.

### Why this is safe for standby

This is the constraint that killed the previous attempt at boot-recovery
instrumentation, which cost the tag Stop3 entirely — 995 uA against 6.56 uA —
so it is worth being explicit:

- **No new flash writes.** The abort marker is already programmed; this fills
  bytes inside it that are already being written as zeros.
- **No new flash region, page or erase.** Capacity and addresses are unchanged,
  and on non-U3 targets nothing changes at all.
- **No backup-register traffic.** The latched bits live in ordinary `.bss` in
  `sensors.c`, valid for the boot that needs them. Nothing is added to
  `pState`, which is where the previous attempt went wrong.
- **Nothing runs on the sleep path.** The hook is called only from
  `recordState()`, which already runs only on transitions.

The idle current must still be measured afterwards, per the rule in
`AGENTS.md`; the argument above is a reason to expect no cost, not evidence.

## Also worth fixing while in here

- `initDataCollection()` should stop at the first failure, or the continuation
  should be justified in a comment. Configuring the IMU FIFO after a sensor has
  already failed leaves more state to unwind and cannot help.
- `writeStoredConfig()` returns `void` and swallows its own verify failure into
  the excluded debug log. It should return a status, and
  `Configured(T_INIT)` should abort with a distinct detail rather than
  proceeding on a configuration it knows is wrong. Not the cause of this
  failure, but the same class of silence.
- `ppm_clock_error` was observed reading `0` where it had earlier read
  `-3.81469727` on the same board. Unexplained, and it feeds timing
  reconstruction. Tracked separately.

## Reproducing

```sh
STOP_ON_FAILURE=1 embedded/tools/power_sweep_imutag.sh 60 results.csv
```

Halts on the first failing point and dumps state, stored configuration and
marker log without resetting. Expect to need one or two sweeps. Detaching the
Joulescope app and qtmonitor first is required; see `AGENTS.md`.
