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

Passed on 2026-10-20 on a BitTag V6 (STM32L4, UUID `2035374D303150190059002F`,
BitTag at `56e5e6a0`, through a TagBase v7):

- a `WriteMemory` of two patterns to `0x20000814`, the padding between `.data`
  and `.bss`, read back correctly, and the original was restored. The map came
  from a rebuild of `56e5e6a0` whose image matched the tag's flash word for
  word. The word's contents drift between sessions, since SRAM1 is not
  retained in Standby;
- `tag-start`, `Start` on the running tag refused with `err == PERM` and
  `transport_failed == false`, `tag-stop`, `tag-dwnld` with a dump identical
  to `main`'s, and `tag-reset`;
- attaching after a timed-out session: 3 of 3 clean on the branch and 1 of 1
  on `main`, so the noisy detection after a timeout did not reproduce;
- unplugging the base during a `GetStatus` loop: after 14 good calls,
  `transport_failed == true` and `err == UNSPECIFIED`;
- `qtmonitor`, checked by the operator. BitTag has no calibration, so
  `qtcalibrate` does not apply.

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
  to one made by `main`'s `tag-dwnld`;
- `tag-reset` from FINISHED, `tag-start`, `tag-stop`, `tag-dwnld` (4 records)
  and `tag-reset` again, each reaching the expected state;
- `Start` on the RUNNING tag refused with `err == PERM`,
  `transport_failed == false` and "Monitor request not permitted in current
  tag state";
- unplugging the base during a `GetStatus` loop: after 27 good calls,
  `transport_failed == true` and `err == UNSPECIFIED`;
- `qtmonitor` attach, status updates, start and stop, and `qtcalibrate` attach
  and status, checked by the operator.

Still to check, then delete each item:

- **Intermittent L4 attach failure through a TagBase v7.** 2 of about 32
  attaches to an IDLE BitTag V6 failed outright: detection logged `read debug
  register 0xe0042000/0xe0044000 failed status=0x81`, then the DEMCR read
  failed and `AttachL4` gave up. Neither followed a timed-out session; both
  followed a clean detach, and 20 back-to-back attaches and 8 after 20 s idle
  were all clean, on `main` and on this branch alike. The code is unchanged by
  this branch apart from the link lock. The leading suspect is that
  `enter_debug_mode()` in `linkadapt.cc` treats a failed SWD enter (the base's
  `SWD_Open`) as a warning and returns true, after which no read recovers;
  the warning is below the default log level, so the failing runs could not
  show it. Next: catch a failure with `--debug` to see that warning, then
  decide between failing the attach and retrying the SWD enter.
- **Noisy detach from a running U3 tag.** Detaching from a RUNNING tag logs a
  burst of `linkadapt.cc` `error return 81` read failures, 41 in one run. They
  come from the MONITORSTOP status poll in `CallU3`: once the firmware handles
  the stop it returns to low power, and each poll fails until the 100 ms
  timeout, which the code already treats as a successful detach. This is
  unchanged from `main`. Quieten it by ending the poll at the first failed
  read once the kick has been delivered, or by logging those reads at debug
  level.

## Verify tag-server on hardware and other platforms

[`tag-server`](../../tagserver/README.md) and
[`tagclient`](../../python/tagclient/README.md) pass their hardware-free tests
on macOS arm64: errors, the debug switch, attaching to a missing base, TCP,
shutdown, and a second server on a busy socket. Still to check, then delete
each item:

- **On an L4 tag and a U3 tag:** with one base connected, run
  `TAG_SERVER_HARDWARE=1 TAG_SERVER_BIN=<build>/Release/bin/tag-server pytest
  host/python/tagclient`. It reads information, status, the state log and
  memory, and holds a 15 s idle session on the server's keepalive. It changes
  no tag state.
- **A state-changing script through the server**, with the operator's
  agreement: `start`, a `start` refused with `ack_err_name == "PERM"`, `stop`,
  paging `get_data_log`, `erase`, and a `write_memory` to an unused word.
  Compare the log pages with `tag-dwnld`'s.
- **Linux and Windows builds** with `BUILD_TAG_SERVER=ON`. On Windows the
  server listens on loopback TCP, and grpcxx has not been built with the
  static `x64-windows-static-md` triplet.
