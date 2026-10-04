---
type: investigation
status: closed
summary: Why a CubeProgrammer loader session changed the next boot's recorded reset cause (RTC_BKP2R 2 -> 1): CubeProgrammer's exit lets the firmware boot, not the loader.
---

# Loader session changes the recorded reset cause (2026-10)

Closed 2026-10-01: the cause is CubeProgrammer's exit, not the loader; a
`tag-xflash` session leaves every backup register unchanged. Extracted verbatim
from the open issues of [Loader Runtime Design](../loader-runtime.md#open-issues).

**A loader session changes the next boot's recorded reset cause.** *Settled
2026-10-01: CubeProgrammer's exit, not the loader.* A `tag-xflash` session,
which downloads and runs the same loader and ends in a plain reset, left every
backup register unchanged. A CubeProgrammer read of the same tag changed
`resetCause` from 2 to 1 and rounded `external_blocks` up to a page, because
CubeProgrammer lets the firmware boot as it leaves. See
`host/libraries/tagcore/design/swd-recovery.md`, step 3.
The original note follows. After any
CubeProgrammer session that uses a loader, read-only or read-write,
`RTC_BKP2R` (`pState->resetCause`) reads 1 (`resetStandby`) where it
otherwise reads 2 (`resetShutdown`). Eight plain connections and a plain
software reset left it at 2. The RTC time and every other backup register were
unaffected. The loader cannot have written the register (see the runtime rules
above). The value is written by the firmware at its next boot, from the reset
flags and the shutdown marker, so the likely cause is how CubeProgrammer leaves
the core after a loader session. This is not confirmed. It matters because
`resetCause` steers boot recovery (`main.c` treats standby and shutdown wakes
differently). A host tool that controls the exit sequence can test it.
