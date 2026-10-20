---
type: worklist
status: current
summary: Open work in tagcore - hardware verification of the monitor side channel, keepalive and LastError added for the tag server proposal.
---

# tagcore TODO

## Verify the monitor side channel on hardware

Phase 1 of the [tag server proposal](design/proposals/tag-server.md) added, in
[`tagmonitor.h`](tagmonitor.h) and [`tagclass.h`](tagclass.h):

- one recursive lock over every SWD link call in `TagMonitor`;
- `ReadMem` and `WriteMem`, exposed as `Tag::ReadMemory` and `Tag::WriteMemory`,
  returning `MemAccess`;
- `KeepAlive`, which writes `host_activity` on U3 and sends `get_status` on L4;
- `Tag::LastError`.

`tag-peek` now uses `Tag::ReadMemory`.
[`tag-side-channel-check`](../../commandline/tag-side-channel-check.cc) runs
the read-only items below and prints PASS or FAIL for each. Use
`--idle <s> --interval <s>` for the keepalive check, and add `--no-keepalive`
for its control run. Three opt-in options change tag memory or state and need
the operator's agreement: `--write-test <addr>`, `--start-running` and
`--unplug <s>`.

Passed on 2026-10-20 on a BitTag V6 (STM32L4, ID code `0x10016435`):

- ID code and 4 KB reads while running;
- the range checks;
- 827 `GetStatus` calls and 374 reads interleaved from two threads with no
  failures;
- `LastError` clean after a successful RPC;
- reads refused after `Detach`;
- `tag-info`;
- the L4 keepalive: 90 s idle with `KeepAlive` every 20 s kept the session,
  and the control run lost it.

Passed on 2026-10-20 on an IMUTag breakout (STM32U3, ID code `0x10016454`,
UUID `00303143433650090059002E`, IMUTagNandBmp581 at `8f1350da`, 3.3 V):

- every read-only check, including `monitor_shared` writes refused with
  `Protected`, and 1720 `GetStatus` calls and 291 reads from two threads with
  no failures;
- the U3 keepalive: 30 s idle with `KeepAlive` every 2 s kept the session
  (15 calls, none failed); the control lost it within 12 s;
- `tag-info`, including straight after a timed-out session, with no link
  errors on the next attach;
- a `WriteMemory` of two patterns to `0x20001070`, the alignment gap between
  `.data` and `.bss` in that build, read back correctly, and the original
  `0x90CC87D6` was restored;
- `tag-dwnld` of a FINISHED log to SQLite: 121 records, with a dump identical
  to one made by `main`'s `tag-dwnld`.

Still to check, then delete each item:

- **No regressions.** `tag-start`, `tag-stop` and `tag-reset` behave as on
  `main` on L4 and U3, and `tag-dwnld` does on L4. `qtmonitor` and
  `qtcalibrate` build, attach and poll status; `qtmonitor` also starts and
  stops.
- **Writes on L4.** `--write-test <addr>` on an unused SRAM word taken from the
  map file of the exact build. On L4 the `monitor_shared` range is not
  guarded, so write only to a word known to be unused.
- **LastError on failures.** `--start-running` on a running tag gives
  `err == PERM` with `transport_failed == false`. `--unplug <s>`, unplugging
  the base mid-call, gives `transport_failed == true`.
- **Noisy attach after a timed-out L4 session.** Attaching after a session has
  timed out logs `read debug register 0xe0042000/0xe0044000 failed
  status=0x81` from target-family detection, because the debug domain is
  down. The attach then succeeds through the L4 path. This branch does not
  change the detection code, so `main` should behave the same, but that is not
  yet confirmed. Quieten the detection, or retry it after the debug domain
  wakes.
