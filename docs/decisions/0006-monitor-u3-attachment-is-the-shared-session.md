---
type: decision
status: accepted
summary: On the U3 mailbox path isMonitorEnabled() is monitorIsAttached() || monitorAttachGraceActive(); DEMCR.VC_CORERESET (MONCONNECTED) is no longer a liveness test, after a latched flag cost U3 tags their sleep.
---

# 0006. Monitor: U3 attachment is the shared session, not VC_CORERESET

Date: 2026-09-03

`isMonitorEnabled()` on the STM32U3 mailbox path stopped consulting
`MONCONNECTED` (`DEMCR.VC_CORERESET`). A host timeout path could leave the flag
set, latching "attached" for the rest of a boot and holding the tag at about
1 mA. Extracted verbatim from
[Tag Monitor Interface](../shared/monitor-interface.md#attachment-state).

## Context, decision and evidence

`isMonitorEnabled()` consulted `MONCONNECTED` on every target, which
contradicted the paragraph above and cost the U3 tags their sleep.

`DEMCR.VC_CORERESET` is set by the host as an attachment flag -- the host code
says so in as many words, at `host/libraries/tagcore/tagmonitor.cc`:

```c
// VC_CORERESET is used as attachment flag to embedded app.
```

The host has two resume paths after a failed monitor call. One clears the flag;
the other, taken when the timeout probe has halted the target, resumes with
`(demcr | MON_EN | VC_CORERESET)` and deliberately leaves it set. Nothing on
the tag clears `VC_CORERESET` except `monitorSharedSetDisconnected()`, which
runs only from a session teardown -- and a teardown needs a session. So one
timed-out monitor call latched "attached" for the rest of that boot:

- every sleep path refused, because they all gate on `isMonitorEnabled()`;
- the main loop took its blocking `chEvtWaitAny()` branch;
- the tag drew about **1 mA instead of 6 uA**, indefinitely;
- only a reset recovered it, because the reset command established a fresh
  session whose teardown then cleared the flag.

Measured on an IMUTagNandBmp581 at 3.29 V: **1037-1040 uA** whenever a monitor
call had timed out, with `demcr=0x1000001` in the timeout dump, against
**5.8-6.3 uA** whenever it had not. Eight logic-analyser channels were silent
throughout the fault -- no NAND, no SPI, no I2C, no sensor data-ready -- so the
current was the core alone, not stranded peripheral activity.

The fix restores what this document specified: on the mailbox path
`isMonitorEnabled()` is `monitorIsAttached() || monitorAttachGraceActive()`,
gated by `TAG_MONITOR_MAILBOX` so L4 keeps vector catch as its attach
mechanism. `MONCONNECTED` is deliberately retained in
`monitorResetRecoveryActive()`, where the question genuinely is whether a
debugger connected under reset; it was only ever wrong as a liveness test.

Verified by 160 attach/detach cycles down to 400 ms attached / 250 ms detached
with zero timeouts and zero errors, the tag answering on every cycle, and idle
at 5.43-5.50 uA afterwards. That test matters because the risk of ignoring the
hint is the opposite failure: a tag that sleeps while a host is still reaching
for it would show up as attach timeouts.
