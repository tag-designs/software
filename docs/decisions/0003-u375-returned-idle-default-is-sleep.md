---
type: decision
status: accepted
summary: IMUTagNand keeps Sleep, not STOP0 or STOP1, as its returned-idle default outside scoped waits.
---

# 0003. STM32U375 returned-idle default is Sleep

Date: 2026-08-16 (commit c068ed9d)

Cut verbatim from [STM32U375 Stop-Mode Support](../../embedded/tags/common/core/design/u375-low-power.md). The
measurements behind it were not recorded.

## Decision

STOP0 and STOP1 were both evaluated as default idle modes for `IMUTagNand`.
The measured power difference was negligible, while the extra STOP transitions
raised reliability concerns, so the target keeps Sleep as its default idle mode.

## Consequences

Deeper modes are used only where a scope sets them: the RUNNING event wait
and the SPI transfer brackets described in
[STM32U375 Stop-Mode Support](../../embedded/tags/common/core/design/u375-low-power.md), and the run sleep in
[the Stop 2 decision record](0007-u375-run-sleep-is-stop-2.md).
