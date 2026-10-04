---
type: decision
status: accepted
summary: The STM32U375 terminal sleep is Stop 3 with an RTC wake through WKUP7 and a synthetic standby reset, because the Standby request is declined in a layout-dependent way.
---

# 0008. STM32U375 terminal sleep is Stop 3, not Standby

Date: 2026-09-07 (commit 0638a76)

The text below is cut verbatim from [Open Issues](../../embedded/tags/design/open-issues.md). The full
search that preceded the decision is in
[the Standby layout investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

## Context

On the STM32U375 a Standby request (`LPMS = 1xx`) is declined in a
layout-dependent way: the `WFI` is reached and the part stays in Sleep at about
1 mA. The search is in
[the Standby layout investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

## Decision

**Resolution (2026-09-07, commit 0638a76):** the terminal sleep now goes
through Stop 3 (`tagPowerEnterStop3()`, RTC wake through WKUP7, synthetic
standby reset on wake). It entered at every layout that stalls Standby and
passed `tag_release_check.py`; cost about 3.6 uA at rest. *Why* the Standby
request is declined remains unknown -- see the two subsections below for what
was established and excluded -- and `tagPowerEnterStandby()` stays in the tree,
unused, as the reference. Everything from here to the next entry is the record
of that search, kept so it is not repeated.

## Alternatives considered

Standby with `__attribute__((noinline))` on `tagPowerEnterStandby()`, and the
compiler settings tabulated in
[the investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md), "The compiler search, exhausted".

## Evidence

#### Stop 3 as the terminal sleep: measured, not yet adopted (2026-09-07)

`tagPowerEnterStop3()` -- the same preparation, `LPMS = 011`, RTC wake through
WKUP7 (`WUSEL7 = 11` selects RTC_ALRA/ALRB/WUT/TS), and `NVIC_SystemReset()`
on wake with `resetCause = resetStandby` -- was wired in as the terminal sleep
and put through the layouts that stall Standby:

| layout | Standby | Stop 3 IDLE | Stop 3 FINISHED |
| --- | --- | --- | --- |
| base (main + non-blocking loop) | IDLE 4.6, **FINISHED 1039** | 8.07 | 8.07 |
| + skip | **1040** | 8.02 / 8.12 | 8.14 |
| + skip + 4 nops | **1040** | 8.08 | -- |
| + skip + 16 nops | **1040** | 8.05 / 8.14 | 8.13 |

A scheduled start (`start_delay: 2`) parked at 7.95 uA and woke on the 12:23:00
minute alarm into an 810 uA run, as the state machine predicts. The cost is
about 3.6 uA at rest against a Standby that works. Whether to ship it is a
decision, not a measurement; the `tag_release_check.py` run on that tree is
the evidence to decide on.

## Consequences

See [the investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md) for what this leaves unexplained.
