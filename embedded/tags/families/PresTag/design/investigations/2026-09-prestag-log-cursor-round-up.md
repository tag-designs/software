---
type: investigation
status: closed
summary: How the PresTag log cursor round-up and hibernation gate used 120 instead of 60 samples, why a brownout with an odd page count desynchronised the log, and the fix (329f1e52).
---

# PresTag Log Cursor Round-Up (2026-09)

Cut verbatim from [`../power-test-plan.md`](../power-test-plan.md) (sections
1.6, 1.7 and 10). The defect was fixed on 2026-09-08 in `329f1e52`
("PresTag: align the log cursor on 60 samples, not 120"). The hibernation-gate
half was confirmed on hardware by the H3 run on 2026-09-09 (see
[`2026-09-prestag-stop2-and-power-campaign.md`](2026-09-prestag-stop2-and-power-campaign.md));
the brownout half (test T4) has not been run. Section numbers (§) refer to the
test plan.

## Hibernation entry gate (from §1.6)

This gate read `sizeof(t_DataLog)/2` — 120, twice the page — until the fix that
accompanies this plan. `external_blocks` counts 4-byte samples, so 120 was the
page measured in 16-bit words, and entry was possible only every *other* block.
The same constant appeared in the `T_INIT` cursor round-up, where it was not
merely coarse; see §1.7.

## The `T_INIT` round-up (from §1.7)

**This read `sizeof(t_DataLog)/2` — 120, twice the page — until the fix that
accompanies this plan.** `external_blocks` counts 4-byte samples (`writeDataLog`
uses `external_blocks * 4`), so 120 was the page measured in 16-bit words. At
every reachable `Running(T_INIT)` the cursor is already a multiple of 60, so the
round-up could only ever break the invariant, never repair it:

- from `Configured`, the cursor is 0 — no-op;
- from a normal hibernation exit, the entry gate had left it 120-aligned — no-op;
- from `restoreLog()` after a **brownout**, the cursor is `60 × pages`. With
  `pages` **odd**, `60 × pages mod 120 == 60`, so it advanced a whole page while
  `pages` stayed put.

After that the next header was written at `vddHeader[pages]` but its samples
landed in page `pages + 1`, and the pairing stayed off by one page **for the
rest of the run**: one block downloaded with a header and zero samples (its page
erased, the reader stopping at the first `pressure == -1`), and every later block
was served samples taken `60 × period` seconds before its header claimed. At the
shipped default of 90 s that is 90 minutes of skew on everything after the
recovery.

The two live routes were `Running(T_INIT, State_EVENT_BROWNOUT)`
([state_machine.c:790](../../../../common/core/src/state_machine.c#L790)) and a
hibernation exit following a brownout, where `restoreLog()` had already replaced
the 120-aligned cursor with `60 × pages`.

Restart recovery is otherwise **benign on a PresTag**, and that is what made this
the whole of the risk. Unlike IMUTag there is no sensor state to rebuild: the
LPS27 is read one-shot per sample, with no FIFO phase, watermark, or streaming
ownership to resynchronise, and the pressure sample taken after a reset is as
good as the one before it. `State_EVENT_BROWNOUT` therefore has nothing to repair
on this family except the log cursor — so the cursor arithmetic in
`Running(T_INIT)` was not one hazard among several, it was the only thing restart
recovery had to get right.

**Both constants were changed together, and had to be.** They were consistent
with each other, which is why normal hibernation exit was unaffected. Correcting
the entry gate alone would have made every odd-page hibernation exit
desynchronise the log — turning a granularity wart into the corruption above.


### Fixed by the change that accompanies this plan

Both were the same constant, and both had to move together (§1.7):

- **Hibernation entry granularity** was 120 samples rather than 60 (§1.6), so a
  window shorter than `120 × period` could be skipped entirely.
- **The `T_INIT` round-up desynchronised the log** after a brownout recovery with
  an odd page count (§1.7), displacing every later block's samples one page from
  its header. A data-integrity defect, not a granularity wart.

**C5** and **T4** are the regression tests. Neither has been run on hardware
yet — the fix is verified only by construction and a clean build of `PresTag`
and `PresTagRaw`.
