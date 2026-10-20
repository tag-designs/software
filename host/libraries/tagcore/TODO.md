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

`tag-peek` now uses `Tag::ReadMemory`. The host build compiles without
warnings, but the Qt applications have not been rebuilt against it, and
nothing has been checked on a tag. Check each item on one STM32L4 tag and one
STM32U3 tag, and delete it when it passes. Most items need a small test
program on `Tag`, beside the other tools in
[`host/commandline`](../../commandline/).

- **No regressions.** `tag-info`, `tag-start`, `tag-stop`, `tag-reset` and
  `tag-dwnld` behave as before. `qtmonitor` and `qtcalibrate` build, attach,
  poll status, start and stop.
- **Reads while running.** `tag-peek` returns the DBGMCU ID code on a running
  tag: `0xE0042000` on L4, `0xE0044000` on U3. A multi-kilobyte
  `ReadMemory` of SRAM, more than one 1 KB chunk, succeeds.
- **Mutual exclusion.** Two threads share one `Tag`, one looping on
  `GetStatus` and the other on `ReadMemory`, for a minute. There are no RPC
  failures or parse errors, and every read returns.
- **Writes.** A `WriteMemory` to an unused SRAM word reads back the same value.
  Take the address from the firmware map file.
- **Guard.** On U3, `WriteMemory` anywhere in `0x20000000`-`0x2000003F`
  returns `Protected`, and the session survives. On L4 the same range is not
  refused; write only to a word known to be unused.
- **Range checks.** An unaligned address or length returns `InvalidRange`.
  After `Detach`, reads return `NotAttached`.
- **U3 keepalive.** Attach, stay idle for 30 s, and call `KeepAlive` every 2 s;
  `GetStatus` still works afterwards. As a control, without `KeepAlive` the
  session ends within about 10 s.
- **L4 keepalive.** The same with a 90 s idle period, calling `KeepAlive`
  every 20 s, against a control without it.
- **LastError.** A `Start` on a running tag gives `err == PERM` with
  `transport_failed == false`. Unplugging the base mid-call gives
  `transport_failed == true`.
