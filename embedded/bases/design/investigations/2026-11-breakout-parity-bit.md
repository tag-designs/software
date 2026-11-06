---
type: investigation
status: closed
summary: Why long loader sessions failed only on tag-breakout-base-l432-v1 (2026-11) - its SWD read checked the turnaround bit instead of the parity bit, so even-parity words read as parity errors whenever the released line rose in time - and the one-line fix, also applied to tag-base-c071.
---

# Breakout base read the turnaround as parity (2026-11)

Closed 2026-11-06: fixed in `tag-breakout-base-l432-v1/src/ll_swd.c` and
`tag-base-c071/src/ll_swd_spi.c`, which took `pbit` from bit 8 of the last
9-bit read instead of bit 7.

## The symptom

On 2026-11-06, while the Rust SWD recovery tools were being accepted
([decision 0045](../../../../docs/decisions/0045-swd-recovery-in-rust.md)),
full 4 MB `tag-xflash dump` and `tag-capture` sessions on the UIUCTag
breakout (UUID `2036354B3032500800520028`) on `tag-breakout-base-l432-v1`
failed with both the Rust and the C++ tools: dozens of status `0x81` replies
per session, each retried, until one failed twice and the session gave up.
512 KB sessions mostly completed. The same tools and loader code never failed
on `bittag-base-v7` with a PresTag or a CompassTag.

The UIUCTag firmware's mailbox monitor was suspected and ruled out: the
recovery session halts the core at its reset vector, clears `DEMCR` and masks
interrupts, and the loader disables them too, so no firmware runs.

## The cause

The data phase of an SWD read is 32 data bits, a parity bit and a
turnaround. The optimized `SWD_TransactionBB()` reads it as `SW_ShiftIn5()`
(turnaround, ACK, data bit 0), `SW_ShiftIn24()` (data bits 1-24) and
`SW_ShiftIn9()`: data bits 25-31 in bits 0-6, **parity in bit 7**, the
turnaround in bit 8. It took `pbit = (tmp >> 8) & 1`, the turnaround. The data
word itself was right, because the parity bit falls off the top when the
word is shifted into place.

The turnaround is the moment the target releases SWDIO. For a word with odd
parity the target had driven the line high, so the sample read 1 and agreed.
For a word with even parity it had driven it low; the sample read 0 while the
line held, and 1 once the pull-up had raised it, which reported a parity
error on a correct word. How often depends on the line's capacitance, the
target supply and the bit-bang timing, so the fault was rig-specific. A blank
flash makes it common: every `0xFFFFFFFF` word has even parity.

The shared `common/src/ll_swd.c`, which `bittag-base-v7` and the other F042
bases build, reads the parity bit correctly. The 9-bit layout came with the
rewrite of this file to SPI-sized transfers in early 2026, and
`tag-base-c071` copied it. `tag-breakout-base-l432-u375-1v8` builds a correct
`ll_swd_spi.c`.

## Evidence

All on the UIUCTag breakout at fw `3a49003b`, with the base built with
`-DUSEEPRINTF=1 -DUSE_CDC_EPRINTF=1` and its log read on its USB serial port.

| Base image | Result |
| --- | --- |
| Unfixed, logging | 112 parity errors over two full dumps, every one `pbit 1` on even-parity data (`0xFFFFFFFF` 91 times; also `0x18`, `0x3C`, `0x4D`, `0x7B`), all on `DRW`/`RDBUFF` reads of block transfers; longest WAIT run 1 of 25, so not a stall |
| Fixed, logging | 0 parity errors over ten sessions: three full dumps (identical), a full `tag-capture` (identical to the dumps), and `tag_rebuild_check.py run` with the Rust tools, which passed; longest WAIT run 1-4 of 25 |

`tag-base-c071` was built with the fix but not run; no C071 base was on the
bench.
