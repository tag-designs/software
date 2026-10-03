# Releasing Tag Firmware and Host Tools

Three procedures, and what each one proves.

**Qualifying a release** takes a candidate image and decides whether it may fly.
CI cannot do this: a build that compiles, links and passes every functional test
can still draw 240x the idle current, because STM32U375 Standby entry depends on
where code lands in the image. Only a bench measurement settles it.

**Programming a tag** takes a qualified image and puts it on hardware, so that
what flies is the image that was archived and measured, and so the board
database can say which bytes are on which tag.

**Releasing the host tools** is a smaller thing and is described last, because
it shares only the tag mechanics. It is here because half of it cannot be done
by CI: the macOS package has to be signed on a machine that holds the Developer
ID key, and that machine is not a GitHub runner.

For how the images are made reproducible in the first place, see
[Tag Firmware Build Reproducibility](tag-build-reproducibility.md). This
document assumes that and concerns itself with what a person does.

## 1. Qualifying a release

### What it proves, and what it does not

`embedded/tools/tag_release_check.py` runs the checks that have each caught a
real regression on this tree. It is a **power** qualification. It says nothing
about whether the sensors read correctly or the protocol is right -- that is
`tag-test`, and it is a separate step.

A pass is a statement about **one image**. Because the build is reproducible, a
commit determines its image, so the result attaches to the release rather than
to the afternoon it was measured. It is still one board: board-to-board hardware
variation is not what this measures.

Qualify **per target**, not per tag. Five distributed targets flying means at
most five runs, on one board each -- not one run per physical tag.

### Qualify the released image, not a rebuild

The image that is measured should be the image that will be flashed. Program the
tag from the release first, then measure what is on it:

```sh
# 1. put the released image on the board, verified against its manifest
python3 <repo>/embedded/tools/flash_release.py <release>/IMUTagNandBmp581 \
    --json ~/tags/qualification.jsonl

# 2. measure what is now on the tag
python3 <repo>/embedded/tools/tag_release_check.py \
    --target IMUTagNandBmp581 \
    --config <repo>/embedded/tools/power-configs/imutag-400.json \
    --skip-build
```

`--skip-build` is what makes this a qualification of the release: without it the
script rebuilds from the working tree and flashes that instead, which measures
something the release process will never produce.

A local rebuild of the same commit is byte-identical, so measuring one would in
practice measure the other. Qualifying the artifact that will actually be
flashed removes the need to rely on that.

### Before starting

- **Detach the Joulescope desktop app and qtmonitor.** Both invalidate the
  result, in different ways, and neither failure is obvious in the output. The
  Joulescope app holds the instrument so the script cannot open it. qtmonitor
  holds the monitor, which keeps `isMonitorEnabled()` true, so the tag never
  sleeps at all and simply reads as a high average.
- **Attach the right board.** Nothing checks that the board matches the target.
  A cross-family mismatch -- an STM32U3 image onto an STM32L4 board -- erases
  and writes before failing to start, so the wrong board loses its contents and
  needs reflashing. A same-family mismatch flashes and runs, silently.
- **Set `--config` for the target.** It defaults to
  `power-configs/imutag-400.json`, and `--target` to `IMUTagNandBmp581`. The
  thresholds below are sized for that configuration.

### What it runs, in order

| Step | What it catches |
| --- | --- |
| build and flash | skipped under `--skip-build`, which is the release case. Otherwise: that the image builds and downloads, with the `.elf` copied into the output directory |
| idle, 4 trials | the Standby stall. A sleeping tag reads about 5 uA and a stalled one about 1035 uA, so the limit is 100 uA and anything between is a failure, not a margin. Repeated because the fault is layout-driven and one reading is not a verdict |
| life-cycle | every resting state, not just idle: idle, running, stopped, idle again. Fails above `--run-max-ua`, default 760 uA against a healthy 665 uA at 400 Hz from a 3.7 V supply -- run current has twice moved ~200 uA between builds differing only in code layout. The shipping board regulates with an SMPS, so both figures scale with supply voltage; the equivalent pair at 3.3 V is 850 and 750 uA |
| attach storms, 3 sets | host/firmware races around attach, which is where they surface |

### Reading and keeping the result

Everything lands in `release-checks/release-<target>-<timestamp>/`:
`results.json` with a verdict per check, `idle1..4.log`, `lifecycle.log`,
`storm1..3.log`, and -- when it built rather than skipped -- the `.elf` it
measured. The script exits non-zero and prints `RELEASE CHECK FAILED`.

Keep the directory. It is the only record that an image was measured.

> **`results.json` does not name the image.** It records the commit of the
> working tree the script ran in, which under `--skip-build` is not necessarily
> the commit of the image on the tag. Write the release tag and the image
> SHA-256 into the directory yourself -- `flash_release.py --json` printed both
> in step 1. A qualification that cannot be matched to the bytes it measured
> proves nothing later.

### Qualifying during development

Rebuilding and flashing from a working tree is the right thing when iterating on
firmware, and is what the script does by default:

```sh
cd <build directory>
python3 <repo>/embedded/tools/tag_release_check.py --target IMUTagNandBmp581 --build-dir .
```

The script prints `NOTE: the tree is dirty; this is not a reproducible release`
when the tree has uncommitted changes. That result is a development signal. It
is not a qualification of anything that can fly, because nothing can reproduce
the image it measured.

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

## Order of operations for a firmware release

1. **Commit and push.** A dirty tree cannot be qualified: nothing can reproduce
   the image it would produce.
2. **Tag `fw-vX.Y`.** CI builds the distributed targets and publishes each image
   with its manifest. The workflow builds with `-DREGENERATE_SOURCES=OFF
   -DREPRODUCIBLE_BUILD=ON`, so a stale generated source, a dirty tree, a
   submodule off its branch or the wrong toolchain fails the build rather than
   shipping quietly.
3. **Download the release.**
4. **Qualify, once per target that will fly.** Attach a board of that type,
   flash it from the release with `flash_release.py`, then
   `tag_release_check.py --skip-build`. Keep the output directory and write the
   release tag and image SHA-256 into it.
5. **Program the field tags** from the same release with `flash_release.py`,
   with `--label` and `--json`.
6. **Run `tag-test`** on each to confirm the hardware works, then `tag-info
   --json`, and enter the board database row.

Steps 4 and 5 are separate because they answer different questions.
Qualification is per image: one measurement clears the image every tag of that
type will receive. Programming is per tag, and each one needs its own row.

A target that is not flying this round does not need qualifying. A target that
is needs it again after any change to its image -- which, the build being
reproducible, means after any change to the commit it is built from.

## 3. Releasing the host tools

Host tools release on their own `vX.Y[.Z]` tags, separate from the `fw-v*`
firmware tags, because they are validated differently: a host tool is qualified
by running it, and needs no bench.

### Why this one is half-manual

The Windows package is built and attached by CI. The macOS package is not.

macOS refuses to launch an app whose signature it does not accept, and the
signature it accepts requires the Indiana University Developer ID certificate.
Putting that certificate and its password into a public repository's secrets
would make the private key recoverable by anyone who can run a workflow on a
fork or land a change to one, so it stays on the developer's machine. CI builds
the macOS package anyway -- ad-hoc signed, as a check that it still builds --
and leaves it as a workflow artifact that nobody ships.

So `release.yml` opens the release as a **draft** with only the Windows ZIP.
The draft is the signal that the release is incomplete. It becomes publishable
once the locally built, Developer ID signed DMG is attached.

The packages are not notarized. macOS blocks an unnotarized Developer ID app on
first launch, and the user clears it once per app through System Settings ->
Privacy & Security -> Open Anyway, or clears quarantine on the whole folder in
one command. Notarization would remove that and nothing else. See
[Installing a macOS Release](../README.md#installing-a-macos-release).

### Steps

Run these on the Mac holding the Developer ID certificate, from a clean
checkout of the commit to be released.

**1. Push the commits.** Not the tag -- the commits. CI builds whatever the tag
points at, and a tag pointing at a commit nobody else has is a release nobody
can reproduce. The script warns if HEAD is not on a remote branch.

**2. Build, sign and verify locally, without pushing the tag.**

```
host/tools/release-macos.sh v3.1 --no-push
```

`--no-push` is the recommended order. The tag is created locally, the image is
built and checked, and nothing has been announced. If anything fails, `git tag
-d v3.1` and the attempt leaves no trace. Without it the script pushes first,
which starts CI and opens a draft release before anything has been verified.

That one command does the whole macOS side:

| It does | Because |
| --- | --- |
| refuses a dirty tree | a release must be reproducible from its commit |
| creates the annotated tag | the DMG is named from the tag, resolved at *configure* time, so the tag has to exist before CMake runs |
| checks the identity is in the keychain | a missing identity otherwise fails deep inside the install step |
| checks the tag CMake resolved | the lookup takes the **highest** reachable `vX.Y` tag, not the newest, so a higher version already merged here would silently name the DMG |
| builds and packages | |
| mounts the DMG and verifies each app | CPack's DragNDrop generator is also handed the signing identity and can re-sign the bundle inside the image; what matters is the signature a user receives, not the one in the build tree |
| prints the DMG path and its SHA-256 | the hash identifies the image that was tested |

Expect thirteen `ok` lines -- five Qt apps, `dataprocessing`, and seven command
line tools. A count that is not thirteen means the install set changed; check
that against `host_cli_install_targets` before shipping.

It stops with an error rather than producing an unshippable image if the
identity is missing, the resolved tag is not the one asked for, or any bundle
in the DMG fails `codesign --verify --strict`. That last check is not
theoretical: it is what caught `tag-attach-cycle.app` and, behind it, the fact
that no command line bundle had ever been signed.

The Gatekeeper assessment it prints reads `accepted / source=Developer ID`.
That is worth understanding rather than trusting: the image was just built
here, so it carries no quarantine attribute, and quarantine is what makes
macOS demand notarization. The assessment confirms the signature is real and
trusted. It says nothing about what a user who downloads the DMG will see --
only the download test in step 5 does.

**3. Push the tag.** CI builds the Windows package and opens a draft release
with the ZIP attached.

```
git push origin refs/tags/v3.1
gh run watch
gh release view v3.1
```

A draft, one asset, the Windows `.zip`, no `.dmg`. A DMG appearing there means
the artifact filter in `release.yml` stopped working.

**4. Attach the macOS package** built in step 2. It matches the tag: the script
refuses a dirty tree, so the image and the commit agree.

```
gh release upload v3.1 ~/Build/tag-designs/software-vcpkg-release/Ultralight-tags-v3.1.dmg
```

**5. Install it from the release page on a Mac that has never seen the build,**
and follow
[Installing a macOS Release](../README.md#installing-a-macos-release) as
written. This is the only step that tests what a user experiences, because it
is the only copy that is quarantined. Everything before it tests the build.

**6. Publish the draft.**

### If something goes wrong

With `--no-push`, almost nothing can: delete the local tag and start over.

Once the tag is pushed, a failure leaves a tag and a draft release behind. Both
are cheap to keep -- fix the problem, commit, and release under the next
number. Re-running the script with the same tag works only if the tag still
points at HEAD; it refuses to move a tag onto a different commit, because a tag
that has been pushed and then moved means different things to different clones.
Deleting a pushed tag is the one recovery worth avoiding. A draft release that
is never published costs nothing.

If the verification reports a bundle you thought you had removed from the
install set, it is probably a stale one being re-packed: CPack's staging tree
is not always cleaned between runs.

```
rm -rf ~/Build/tag-designs/software-vcpkg-release/_CPack_Packages
```
