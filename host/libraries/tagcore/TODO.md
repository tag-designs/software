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
for its control run.

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

Still to check, then delete each item:

- **STM32U3: everything.** Run `tag-side-channel-check --idle 30 --interval 2`,
  then the control with `--no-keepalive`; the session should end within about
  10 s. The U3 run also checks that the `monitor_shared` write guard returns
  `Protected`.
- **No regressions.** On L4 and U3, `tag-start`, `tag-stop`, `tag-reset` and
  `tag-dwnld` behave as before, and on U3 `tag-info` does too. `qtmonitor` and
  `qtcalibrate` build, attach, poll status, start and stop.
- **Writes.** A `WriteMemory` to an unused SRAM word reads back the same value.
  Take the address from the firmware map file. On L4 the `monitor_shared`
  range is not guarded, so write only to a word known to be unused.
- **LastError on failures.** A `Start` on a running tag gives `err == PERM`
  with `transport_failed == false`. Unplugging the base mid-call gives
  `transport_failed == true`.
- **Noisy attach after a timed-out L4 session.** Attaching after a session has
  timed out logs `read debug register 0xe0042000/0xe0044000 failed
  status=0x81` from target-family detection, because the debug domain is
  down. The attach then succeeds through the L4 path. This branch does not
  change the detection code, so `main` should behave the same, but that is not
  yet confirmed. Quieten the detection, or retry it after the debug domain
  wakes.
