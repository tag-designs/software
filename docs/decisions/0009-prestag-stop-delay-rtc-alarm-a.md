---
type: decision
status: accepted
summary: PresTag stop delays count matches of a free-running RTC Alarm A instead of re-arming LPTIM1, whose register synchronisation at a 1024 Hz LSE cost more Run current than the delays themselves.
---

# 0009. PresTag stop delays use a free-running RTC Alarm A, not a re-armed LPTIM1

Date: 2026-09-09

Cut verbatim from sections 1.2b and 1.2c of
[the PresTag power test plan](../../embedded/tags/families/PresTag/design/power-test-plan.md);
implemented in `0ac8bc6`. Section numbers (§) refer to that plan. The rest of
the diagnosis is in
[the campaign investigation](../../embedded/tags/families/PresTag/design/investigations/2026-09-prestag-stop2-and-power-campaign.md).

## Context

### 1.2b Measured: `stopMilliseconds()` spins ~6.7 ms and never sleeps

Every driver wait on this target goes through `stopMilliseconds()`, which arms
LPTIM1 for the requested delay and enters Stop 2 with `WFE`. Instrumented with
retained timestamps (see §11), the call behaves like this for requests of 2 ms
and 5 ms alike:

| Step | Measured |
| --- | --- |
| bus disable, LPTIM clock/enable | ~0.1 ms |
| **`ARR` write → `ARROK` seen** (`while (!(ISR & ARROK)) {}`) | **6.3–7.1 ms, Run current** |
| `WFE` loop until `ARRM` | **0.0–0.1 ms, exactly 1 iteration** |

The cause is the clock. **LSE on this board is 1024 Hz**, and LPTIM1 runs from
it (`RCC_CCIPR.LPTIM1SEL = 11`, read live), so `TAG_STOP_LPTIM_HZ = 1024` is
right and a 5 ms request correctly programs 6 ticks. But LPTIM register writes
— `ARR`, and every `ICR` flag clear — only take effect after kernel-clock edges,
and at 1024 Hz each edge is ~1 ms. The `ARROK` wait therefore burns ~7 cycles of
Run current, longer than the delay it is arming; and the `ARRM` clear written
immediately before the `WFE` has not propagated when the loop reads `ISR`, so
it sees the *previous* call's match still set and returns without sleeping.
Net effect: **the Stop 2 sleep never happens, the wait lasts ~6.7 ms whatever
was requested, and all of it is at Run current** — three per sample (two LPS27
waits, one flash-wake wait), ~20 ms and roughly a third of `Q_cycle`, the flash
one dearest because the AT25 is awake underneath it.

The AT25 recharge metering is therefore over-satisfied in time (7 ms for a 2 ms
request) but paid for at Run current instead of in Stop.

Fix direction: the synchronisation cost has to become small relative to the
delay, and the flag check must not race the clear. Options, in rough order of
preference: (a) clock LPTIM1 from LSI (~32 kHz, available in Stop 2; `LPTIM1SEL`
is independent of `RTCSEL`, so the RTC stays on the 1024 Hz LSE) with a
prescaler to taste and `TAG_STOP_LPTIM_HZ` updated — sync drops to tens of µs;
(b) keep 1024 Hz but never rewrite `ARR`: free-run, use `CMP = CNT + ticks` and
wait for `CMPOK` *by sleeping*, accepting ~1–2 ms of granularity; (c) the RTC
wakeup timer, which has the same LSE-domain sync cost. Whichever is chosen must
then be shown to sleep — a dip to ~2 µA for the requested duration in a fine
trace — rather than argued.

## Decision

**The LPTIM arming cost.** Re-arming LPTIM1 per delay costs an `ARROK`
busy-wait of 6.3-7.1 ms at Run current on a 1024 Hz LSE, longer than the delay
it arms. Replaced by a free-running RTC Alarm A tick (§1.2d).

## Evidence

| build | `Q_cycle` | `I_avg` at 10 s |
| --- | --- | --- |
| LPTIM, floating pin | 34.06 µC | 3.6948 µA |
| LPTIM + pin fix | 26.82 µC | 2.9742 µA |
| **Alarm A + pin fix** | **14.49 µC** | **1.7376 µA** |

## Consequences

The mechanism as built is described in §1.2d of the test plan.
