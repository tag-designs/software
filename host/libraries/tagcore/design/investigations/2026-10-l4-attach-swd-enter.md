---
type: investigation
status: closed
summary: Why about one attach in 150 to an idle STM32L432 tag failed with status 0x81 on its first debug read - the SWD enter sent just after nRST wakes the tag from Shutdown occasionally reaches the debug port before it is powered, and the host ignored the failed enter - and the retry that fixes it in both tagcore libraries.
---

# Rare L4 attach failure: SWD enter during the wake from Shutdown (2026-10)

Closed: fixed in both tagcore libraries, which now retry a failed SWD enter
with nRST still held (`LinkAdapt::enter_swd` in `tagcore-rs`,
`LinkAdapt::enter_debug_mode` in `linkadapt.cc`: up to four sends, 10 ms
apart).

## The cause

An idle or finished BitTag sleeps in Shutdown (`BitTag/inc/custom.h`), where
the core domain, and with it the SW-DP, is unpowered. An attach drives nRST
low, which wakes the part, and then sends the ST-LINK enter command, on which
the base runs `SWD_Open()`: line reset, DP ID read, debug power-up. The enter
reaches the base about 0.7 to 1.2 ms after the reset command; the L432 needs
about 250 us to wake from Shutdown. Occasionally the two USB commands land
closer together than that, the DP does not answer, and `SWD_Open()` fails at
its first step, in 0.5 ms.

The failure was then hidden. Both libraries logged a non-OK enter status as
a warning, below the tools' default log level, and continued; the ST-LINK
mode read-back that was meant to confirm the enter reports debug mode (0x2)
either way. The base does not reopen the port on a plain debug-register
read, so every later read in the session failed with 0x81
(`STLINK_DEBUG_ERR_FAULT`), starting with DBGMCU_IDCODE during family
detection. A new process a moment later attached cleanly, because the
failed attempt had woken the tag.

This is not the DBG_SLEEP fault of
[the 0x81 investigation](../../../../../embedded/bases/design/investigations/2026-10-0x81-dbg-sleep.md):
that one stalled the bus during a session; this one happens before a session
exists.

## Evidence

2026-10-31, BitTag V6 `2039354D30315012005E0068` (firmware `eb003aee`) on
`bittag-base-v7` (`8009001A5556431520303537`, firmware `eb003aee`), powered
by the base at 3.23 V. A probe program drove the link layer directly: a real
monitor session through `Tag`, a gap, then an instrumented enter (timed, its
status captured), an IDCODE read and, on failure, a second enter with nRST
still held.

| Test | Result |
| --- | --- |
| Attach failures seen | 4 (one in a `tag-info` sweep of 40, three in the probe's 640 enters), each with a failed enter |
| Failed enters without an attach failure | 0 in about 560 |
| Enter time | 0.7 to 1.2 ms normally (5.4 ms at most); 0.5 ms when it failed |
| Enter on a sleeping tag with nRST released | failed 10 of 10, 0.5 ms: the DP is dead in Shutdown |
| Tag asleep after a session | 40 of 40 at gaps of 0 to 50 ms after detach, and every gap up to 2 s |
| Enter again after a real failure, nRST held, 20 ms later | recovered 1 of 1 |
| Dead DP, then nRST, then enter 0, 1, 2 or 5 ms later | recovered 97 of 97 |
| A 10 ms pause between nRST and the first enter | 0 failures in 120, too few to call it a fix |

The rate, about one enter in 150, is why it looked like a rare and
history-dependent fault: the 2026-10-20 note in `../../TODO.md` saw 2 of
about 32, and 2026-10-31's bench session three in a dozen attaches made a
few seconds apart.

## The fix

Treat the enter's status as the result it is: while the probe reports the
enter failed, wait 10 ms (forty times the wake) and send it again, up to
four sends. nRST is held by the caller throughout, so the tag stays in reset.
A retry that succeeds is logged at WARN; four failures are logged at ERROR
and the attach continues as before, so the change cannot fail an attach
that used to work. The base could retry `SWD_Connect()` itself as well,
which would also cover other host software; that is not done.

With the fix in `tagcore-rs`, on the same tag and base:

| Test | Result |
| --- | --- |
| 300 cycles at a 0.3 s gap (600 enters) | no failed enter, no failed attach or read |
| 600 cycles at a 1.5 s gap (1,200 enters) | 36 first sends failed, all 36 succeeded on the second; no failed attach or read |
| Enter on a sleeping tag with nRST released (forced) | four sends in 39 ms, one ERROR line, and the sequence continued; nRST then woke the tag and the next enter succeeded |

The C++ build ran `tag-info` 20 times without a failure; no first send failed
in those, so its retry ran only as compiled code. The failure rate of the
first send varied between runs, from none in 600 to 36 in 1,200, as a race
between two USB commands would.
