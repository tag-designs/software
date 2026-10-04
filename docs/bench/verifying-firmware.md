---
type: procedure
status: current
summary: How to verify a firmware change -- confirm what compiled, rebuild cleanly, compare disassembly rather than checksums, measure idle after power-path changes, and distrust host tools and checks until they are shown right.
---

# Verifying a Firmware Change

A clean build and passing functional tests prove nothing about sleep. After a
firmware change:

1. confirm which source file actually compiled;
2. clean-rebuild the target if you changed a type that common code uses;
3. compare `.list` disassembly between commits, never ELF checksums;
4. run the life-cycle check if the change touches boot, the state machine or
   power sequencing;
5. poll host tools for the state you asked for, rather than trusting an
   acknowledgement;
6. when a check fails, confirm what it measured before believing it;
7. before a release, qualify the image on hardware.

Steps 4 and 7 need the rig. Set it up as described in
[power-testing.md](power-testing.md).

## 1. Confirm which source compiled

A tag-local `src/<name>.c` replaces the family or module file with the same
basename for that target only. So the file you edited is not always the file
that was built. The dependency file says which one was:

```sh
grep <name>.c <build-dir>/embedded/tags/<Tag>/dep/<name>.o.d
```

It prints `src/<name>.c` for a tag-local override, and the family or module
path otherwise.

## 2. Clean-rebuild after changing a shared type

An object compiled from common sources keeps the header it resolved when it was
built. If you change a type that a tag supplies to common code, a stale object
can stay linked against the old definition, and nothing warns you. The live
example is `t_DataHeader`, which types the `vddHeader[]` array declared in
`common/core/src/persistent.c`. Remove the target's `build/` and `dep/`
directories and build again.

## 3. Compare disassembly, not checksums

Every image embeds the git hash through the generated `version.h`. So every
binary changes when HEAD moves, including targets the commit did not touch.
Compare the `.list` disassembly instead. A checksum comparison means something
only between two builds at the same commit in the same tree.

## 4. Measure idle after any change to the power path

Measure after any change that touches any of these:

- the boot cleanup in `main.c`;
- `state_machine.c`;
- `pwr.c` and the per-MCU `pwr-l432.c` and `pwr-u375.c`;
- `godown()`;
- `pState`;
- device power sequencing.

An argument is not enough here; take a measurement. The live example (commit
bf7b4b10): an IMUTagNandBmp581 build that added six retained backup-register
words and a status line (`TAG_RECOVERY_TRACE`) sat awake in WFI instead of
entering its terminal sleep, at 995 uA against 6.56 uA, a 150x idle penalty.
Every functional test passed across four flashes. The added code turned out
not to be the cause -- see the bisect note below -- which is the point: the
cost appeared on a change that should have had none. The `debug_log` module is
excluded from shipped images for a separate reason: it prevents low-power entry
on the U375 by a fault of its own, at 1.71 mA, a different signature from the
995 uA one ([TODO](../../embedded/tags/TODO.md)).

Before measuring, **ask the user to detach the Joulescope desktop app and
qtmonitor, and wait for confirmation.** The reasons are in
[power-testing.md](power-testing.md#1-before-measuring). Then run the life-cycle
check, which takes the tag through idle, running, stopped and idle again and
measures every resting state. `--use-server` needs `joulescope_server.py
--start` running first ([power-testing.md](power-testing.md#2-the-joulescope-server)):

```sh
embedded/tools/tag_lifecycle_check.py \
    --config embedded/tools/power-configs/imutag-400.json --run-duration 60 --use-server
```

Use the target's own config and bounds from its plan. If you measure only one
state, you learn only about that state. The life-cycle check measures idle with
the clock set, which is how a prepared tag is actually left. It also compares
idle before the run with idle after it. Those are the same state reached by two
histories, and that comparison is where a state-dependent fault shows up even
when both numbers look plausible. Then run `tag_attach_storm.py` for attach and
clock-cycle reliability. It measures no power; the two tools complement each
other.

When a change might shift code layout (on the U375, any change might), also see
[U375 low-power behaviour](../../embedded/tags/common/core/design/u375-low-power.md)
and decisions [0007](../decisions/0007-u375-run-sleep-is-stop-2.md) and
[0008](../decisions/0008-u375-terminal-sleep-is-stop-3.md). On that part, a
change that provably cannot alter behaviour has moved idle current by orders of
magnitude. Suspect layout before logic.

### When the number is wrong, bisect

Do not reason about it. Bisect against the last known-good image:

1. stash the firmware changes;
2. rebuild, flash and measure;
3. restore the changes.

That tells "my change did this" apart from "this board is different today" in
one step. A bisect tells you *which* change moved the number, not *why*. The
995 uA above bisected to a single added read of a flash-resident marker field.
It was explained first as the instrumentation's cost, which fell when the same
instrumentation, rebuilt with a pre-sleep flag clear, measured 6.705 uA against
6.716 uA without it, and then as a
latched flash error flag, which fell when a failing build was captured with the
flash error flags clean. On the U375 a one-line change can move the image
layout, and the cause was never pinned on the code itself
([investigation](../../embedded/tags/common/core/design/investigations/2026-09-u3-latched-flash-error-flags.md),
[layout investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md)).
Look at `git log` before forming a theory: a
fault chased for most of a day through RTC alarm teardown was a commit from
the previous afternoon. Take three or four trials per point.
[Debugging a tag](debugging-a-tag.md) covers the cheap explanations to rule out
first, such as a pin driven against a pull-up.

## 5. Host tools: an acknowledgement is not a completion

`Tag::Stop()` and `Tag::Erase()` return true when the request is *accepted*.
The monitor handler only sets a work bit, and the state machine acts later. A
host tool that reads the status straight afterwards sees the old state. That is
how `tag-stop` exited 0 while the tag was still RUNNING. The download then
refused with "Can't dump logs from current state", which was read for a long
time as an intermittent download bug. Poll for the state you asked for, a few
seconds apart, and fail with the last state seen.

`Tag::Attach()` connects under reset, so the tag is still booting. Its first
status can legitimately report `STATE_UNSPECIFIED`, because `pState->state` is
zero until the state machine restores it. **Any tool that sends a request
straight after attaching must first wait for a definite state.** Missing this
has broken three tools, and each failure looked like a different intermittent
fault:

- `tag-reset` skipped its erase and reported success for a reset that never
  happened;
- the `SetRtc` sent next was rejected;
- `tag-stop` was refused with "Monitor request not permitted in current tag
  state", which then surfaced as a download failing with "Can't dump logs from
  current state".

`tag-reset`, `tag-start` and `tag-stop` now poll for a real state, a second
apart, bounded by `--settle-timeout`.

## 6. When a check fails, confirm what it measured

A failing check is a claim about the tag. Treat it with the same scepticism as
any other measurement. `check_download()` in `power_experiment.py` picks a
timestamp column by name, and once it picked the wrong one. `ImuAccel`
declares `RawElapsedUs` before `ElapsedUs`. `RawElapsedUs` counts elapsed
microseconds from the segment start, so it restarts at zero in every segment.
The tool reported one "non-monotonic timestamp" per restart-recovery, and that
was filed as an intermittent firmware fault for some time. The correct column
was monotonic throughout. A wrong check also hides real faults behind it,
which is how an intermittent download failure went unseen.

Do not relax a failing check to make a run pass until you have shown that the
check is wrong.

## 7. Qualify a release on hardware

Run `tag_release_check.py` on the image that will ship. See
[power-testing.md](power-testing.md#6-the-qualification-tools) for what it runs
and why each step exists, and [the release procedure](../release/release-procedure.md)
for the steps.
