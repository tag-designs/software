# Releasing and Programming Tag Firmware

Two procedures, and what each one proves.

**Qualifying a release** takes a candidate image and decides whether it may fly.
CI cannot do this: a build that compiles, links and passes every functional test
can still draw 240x the idle current, because STM32U375 Standby entry depends on
where code lands in the image. Only a bench measurement settles it.

**Programming a tag** takes a qualified image and puts it on hardware, so that
what flies is the image that was archived and measured, and so the board
database can say which bytes are on which tag.

For how the images are made reproducible in the first place, see
[Tag Firmware Build Reproducibility](tag-build-reproducibility.md). This
document assumes that and concerns itself with what a person does.

## 1. Qualifying a release

### What it proves, and what it does not

`embedded/tools/tag_release_check.py` builds the target, flashes it, and runs
the checks that have each caught a real regression on this tree. It is a power
qualification. It says nothing about whether the sensors read correctly or the
protocol is right -- that is what `tag-test` is for, and it is a separate step.

A pass is a statement about **one image on one board**. It does not transfer to
another build of the same commit unless that build is byte-identical, which is
why the reproducibility work matters: it is what lets a qualification result
attach to a commit rather than to a particular afternoon.

### Before starting

- **Detach the Joulescope desktop app and qtmonitor.** Both invalidate the
  result, in different ways, and neither failure is obvious in the output. The
  Joulescope app holds the instrument so the script cannot open it. qtmonitor
  holds the monitor, which keeps `isMonitorEnabled()` true, so the tag never
  sleeps at all and a held monitor simply reads as a high average.
- **Attach the right board.** The script flashes whatever target is named onto
  whatever board is connected, and nothing checks that they match. A
  cross-family mismatch -- an STM32U3 image onto an STM32L4 board -- erases and
  writes before failing to start, so the wrong board loses its contents and
  needs reflashing. A same-family mismatch flashes and runs, silently.
- **Commit first.** The script records the commit and whether the tree was
  dirty, and prints `NOTE: the tree is dirty; this is not a reproducible
  release`. A qualification of an uncommitted tree cannot be tied to anything.
- **Match the toolchain.** `ARM_TOOLCHAIN_VERSION` pins 14.2.1; a different
  compiler is a different image and configure will say so.

### Running it

```sh
cd <build directory>
python3 <repo>/embedded/tools/tag_release_check.py \
    --target IMUTagNandBmp581 \
    --build-dir .
```

`--target` defaults to `IMUTagNandBmp581` and `--config` to
`embedded/tools/power-configs/imutag-400.json`; both need setting for another
tag. `--skip-build` measures the image already on the tag instead of rebuilding
and reflashing, which is the option to use when qualifying a released binary
rather than a working tree.

### What it runs, in order

| Step | What it catches |
| --- | --- |
| build and flash | that the image builds and downloads at all; the `.elf` is copied into the output directory, so the measured image is kept |
| idle, 4 trials | the Standby stall. A sleeping tag reads about 5 uA and a stalled one about 1035 uA, so the limit is 100 uA and anything between is a failure, not a margin. Repeated because the fault is layout-driven and one reading is not a verdict |
| life-cycle | every resting state, not just idle: idle, running, stopped, idle again. Fails above `--run-max-ua`, default 850 uA against a healthy 750 uA at 400 Hz -- run current has twice moved ~200 uA between builds differing only in code layout |
| attach storms, 3 sets | host/firmware races around attach, which is where they surface |

### Reading the result

Everything lands in `release-checks/release-<target>-<timestamp>/`:
`results.json` with a verdict per check, `build.log`, `idle1..4.log`,
`lifecycle.log`, `storm1..3.log`, and the `.elf` that was measured. The script
exits non-zero on failure and prints `RELEASE CHECK FAILED`.

Keep the directory. It is the only record that a given image was measured, and
`results.json` carries the commit it was built from.

> **The image hash is not in `results.json`.** Record it alongside, from the
> build manifest or from `flash_release.py`. A commit does not identify an
> image, so a qualification recorded against a commit alone cannot later be
> matched to the bytes that were measured.

## 2. Programming a tag with a released binary

### The rule

**A field tag is programmed from a release, never from a build tree.**

A release is built by a machine nobody edits, from a commit, and ships a
manifest recording the hash of every image. A build tree is whatever a developer
had that afternoon. `<Tag>-download` programs the latter, which is right during
development and wrong for the field: the image that flies is then never the
image that was archived and measured, and nothing afterwards can say which bytes
went onto the tag.

The same rule applies to qualification. Measure the released image -- with
`--skip-build`, against the binary from the release -- rather than a local
rebuild of the same commit. The two are now byte-identical, which is what makes
the substitution safe, but qualifying the artifact that will actually be flashed
removes the need to rely on that.

### Steps

**1. Unzip the release** on the machine with the ST-LINK attached. No build
tree, CMake configure or toolchain is needed -- a release directory is
self-describing, with `BitTag.elf` beside `BitTag-build-manifest.json`.

**2. Program, with the board's label:**

```sh
python3 <repo>/embedded/tools/flash_release.py <release>/BitTag \
    --label BitTag-017 \
    --json ~/tags/programmed.jsonl
```

This checks the image against the SHA-256 its manifest records and refuses to
program on a mismatch, prints the provenance it verified, then programs through
the same probe selection the CMake targets use. `--verify-only` checks without
programming. `--selector` picks an ST-LINK when several are attached.

It does **not** check that the image belongs on the attached board, and cannot:
the programmer reports the MCU, not the board, and four of the five distributed
tags are `stm32l4xx`. Boards are labelled by hand for this reason, and `--label`
records which one was claimed rather than verifying it.

**3. Confirm the tag runs and read back what it says:**

```sh
tag-test                 # the usual check that the hardware works
tag-info --json          # machine-readable, for the record
```

**4. Record the row** in the board database. Between the two files:

| From | Fields |
| --- | --- |
| `flash_release.py --json` | board label, **image SHA-256**, commit, toolchain, ChibiOS commit, whether programming succeeded |
| `tag-info --json` | chip UUID, commit, build time, board description |

The two records cannot be joined automatically, and deliberately so: a batch
flashed from one release shares a commit and an image hash across every record,
distinguished only by the label the operator supplies, while the tag knows its
UUID and not its label. Pairing is a per-tag step -- flash one, read one, enter
one row.

### The field that only exists now

**The image SHA-256 can only be captured at programming time.** A tag reports
which commit it was built from, forever. It can never report which *build* of
that commit, because an image cannot contain its own hash. `build_time` is the
commit date, not a compile time, so it does not distinguish builds either.

If the hash is not written into the row when the tag is programmed, it is gone,
and the tag can afterwards be tied only to a commit. That is enough when the
build is byte-reproducible, and is not enough when it is not -- which is why the
hash is recorded regardless.

## Order of operations for a release

1. Commit and push. A dirty tree cannot be qualified.
2. Tag `fw-vX.Y`. CI builds the distributed tags and publishes images with
   their manifests.
3. Download the release.
4. Qualify each tag: attach the right board, run `tag_release_check.py`
   with `--skip-build` against the released image, keep the output directory,
   and record the image hash with the result.
5. Program field tags from the release with `flash_release.py`, recording the
   label and hash.
6. Run `tag-test` on each, and enter the row.

Steps 4 and 5 are separate on purpose. Qualification is per image; programming
is per tag. Qualifying one board does not qualify the others, but it does
qualify the image they all receive.
