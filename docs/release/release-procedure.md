---
type: procedure
status: current
summary: How to qualify a firmware image on the bench, program and record a field tag from a release, and cut a signed host tools release.
---

# Releasing Tag Firmware and Host Tools

Three procedures, and what each one proves.

**Qualifying a release** takes a candidate image and decides whether it may fly.
CI cannot do this: a build that compiles, links and passes every functional test
can still draw 240x the idle current, because on this hardware whether a tag
reaches its sleep mode has depended on where code lands in the image (first
seen with STM32U375 Standby, which is why the shipping U375 terminal sleep is
Stop 3). Only a bench measurement settles it.

**Programming a tag** takes a qualified image and puts it on hardware, so that
what flies is the image that was archived and measured, and so the board
database can say which bytes are on which tag.

**Releasing the host tools** is a smaller thing and is described last, because
it shares only the tag mechanics. It is here because half of it cannot be done
by CI: the macOS package has to be signed and notarized on a machine that holds
the Developer ID key, and that machine is not a GitHub runner.

For how the images are made reproducible in the first place, see
[Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md). This
document assumes that and concerns itself with what a person does.

## Release tags and what CI builds

Host tools and tag firmware release on separate tag namespaces, because they are
validated differently. A host tool release is exercised by running it; a firmware
release has to be bench-tested on hardware for power behaviour before it can fly.
Tying them together would either demand that validation every time a host tool
ships, or invite it to be skipped.

| Tag | Workflow | Produces |
| --- | --- | --- |
| `vX.Y`, `vX.Y.Z` | `release.yml` | a draft release with the Windows ZIP of the host tools, and an ad-hoc signed macOS DMG that is re-signed and added locally |
| `fw-vX.Y`, `fw-vX.Y.Z` | `release-firmware.yml` | images and build manifests for the tags marked `DISTRIBUTE` |

The host version lookup filters tags on `v[0-9]*.[0-9]*`, so `fw-v` tags do not
affect host package naming.

**Tag firmware.** `.github/workflows/release-firmware.yml` builds firmware for
the tags marked `DISTRIBUTE` and attaches one archive,
`tag-firmware-fw-vX.Y.tar.gz`, to the release. It holds `firmware/<Tag>/` for
each tag: the `.elf`, `.map`, `.bin`, `.hex`, `.dmp` and `.list`, with the
build manifest beside them. The distributed external loaders are built by the
same target but are not collected into the archive. The manifest records the
commit, whether the tree was dirty, the ChibiOS commit and the branch it tracks, the toolchain and nanopb versions, and
the SHA-256 of the image itself. That hash, not the commit, is what identifies a
build: a `-D` leaves no trace in the git hash, so two materially different
images can report the same commit.

The job never regenerates. It configures with `-DREGENERATE_SOURCES=OFF
-DREPRODUCIBLE_BUILD=ON`, so the committed generated sources are used as they
are, and a stale one -- along with a dirty tree, a submodule off the branch
`.gitmodules` tracks, or a toolchain other than the pinned 14.2.1 -- is a
configure error rather than something quietly worked around. It installs that
toolchain from Arm's own tarball, verified against the SHA-256 in the workflow,
because Ubuntu's packaged `gcc-arm-none-eabi` is a different version and the pin
would reject it.

**Host tools.** `.github/workflows/release.yml` builds the Windows and macOS host
packages on GitHub Actions. Pushing a `vX.Y` or `vX.Y.Z` tag builds both
platforms and opens a **draft** release with the Windows ZIP attached. The
packages take their version from the same `git tag --merged HEAD` lookup used by
local builds, so the workflow checks out full history. A `workflow_dispatch` run
performs the same builds and uploads the packages as workflow artifacts without
publishing anything, which is the way to exercise the pipeline without cutting a
tag.

| | Windows | macOS |
| --- | --- | --- |
| Runner | `windows-2022` | `macos-15` (arm64) |
| Generator | Visual Studio 17 2022 | Ninja |
| vcpkg triplet | `x64-windows-static-md` | `arm64-osx-static` |
| Qt | `QT_VERSION` in the workflow env, installed with aqt | same |

The workflow checks out vcpkg at the baseline commit recorded in
`vcpkg-configuration.json` rather than using the runner's preinstalled copy, so
manifest resolution is reproducible, and caches built ports keyed on
`vcpkg.json` and `cmake/vcpkg-triplets/`. Host user guides are built into the
packages (`BUILD_HOST_DOCS=ON`), matching a local package build. The macOS
package CI builds is ad-hoc signed, carries a build provenance attestation, and
is not attached by CI. It is re-signed with the Developer ID identity on a Mac
and uploaded from there
([decision 0024](../decisions/0024-release-macos-package-built-on-ci-signed-locally.md));
section 3 covers the local half.

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

`tag_release_check.py` defaults to `IMUTagNandBmp581` and an IMUTag config, and
its thresholds are sized for that board. It cannot drive a BitTag: the life-cycle
walk it runs fails on the first attach to a sleeping BitTag, so BitTag is
qualified by hand following its
[power test plan](../../embedded/tags/BitTag/design/power-test-plan.md).

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
- **Have the host tools and the Joulescope interpreter in place.** The script
  resets the tag with `<repo>/build-host/bin/tag-reset`, so build the host tools
  into `build-host/` first, and it measures with `--measure-python`, which
  defaults to `~/opt/joulescope-mcp/.venv/bin/python`; pass an interpreter that
  can import `pyjoulescope_driver` if yours is elsewhere.
- **Set `--config` for the target.** It defaults to
  `power-configs/imutag-400.json`, and `--target` to `IMUTagNandBmp581`. The
  thresholds below are sized for that configuration.

### What it runs, in order

| Step | What it catches |
| --- | --- |
| build and flash | skipped under `--skip-build`, which is the release case. Otherwise: that the image builds and downloads, with the `.elf` copied into the output directory |
| idle, 4 trials | a tag that fails to sleep, such as the layout-dependent stall first seen with U375 Standby. A sleeping tag reads about 5 uA and a stalled one about 1035 uA, so the limit is 100 uA and anything between is a failure, not a margin. Repeated because the fault is layout-driven and one reading is not a verdict |
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
> SHA-256 into the directory yourself. `flash_release.py --json` prints nothing;
> it appends a record to the named file, whose `sha256` field is the image hash
> and whose `describe` field carries the release tag when the build was at one. A qualification that cannot be matched to the bytes it measured
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

### When a mass erase is needed, and what it costs

A normal program writes only the loaded image. The NOLOAD regions beside it --
`.calibration`, the stored configuration, `.persistent`, the NAND map -- are
not written, so **they survive a firmware update**. That is usually what you
want: a provisioned tag keeps its calibration across an upgrade.

It stops being what you want when an upgrade changes where those regions are or
what is in them. The STM32L432 script places `.calibration` as
`(NOLOAD): ALIGN(2048)` straight after the code, so it moves whenever the image
grows; the STM32U375 script pins both bounds and asserts that code cannot reach
them. On a part of the first kind, a major upgrade can leave a region's bytes
at an address the new firmware does not read, or read a region written in a
format it no longer understands. Neither is reported. **For a major upgrade on
such a target, mass erase and reprovision**, rather than trusting the old
contents.

**Erase and program in one invocation.** `flash_release.py --erase` passes
`-e all` and `-d <image>` to the same programmer call, which is what avoids the
bootloader problem rather than recovering from it: two separate invocations
leave a reset between them with the flash empty, and that is what latches
`FLASH_SR.PEMPTY`. With `nSWBOOT0 = 1` and BOOT0 low a set PEMPTY sends every
reset to the ROM bootloader -- the firmware does not run, no monitor answers,
and `tag-info` reports the condition rather than the tag. Hardware re-evaluates
the flag only at power-on or an option-byte load, so a tag in that state must
be power-cycled before it will run the image just written.

**Reprovisioning is the price, and it is a small one.** A mass erase destroys
the NOLOAD regions along with the image, so a calibrated tag needs
recalibrating. For the handful of boards in this project that is cheaper than
carrying a migration path, and it is the agreed approach for the release that
relocates CompassTag's calibration.

Two traps around it:

- **Do not "clear" the flag.** Writing 1 toggles it, so a tool that clears it on
  a part where it was already clear will set it instead. Power-cycle, or issue
  an option-byte load.
- **A byte-level backup of a region is not a backup of its meaning.** Dumping
  `.calibration` before a mass erase and writing the same bytes back afterwards
  produced a byte-identical readback and calibration that did not work
  (CompassTagAT25, 2026-10-04). Reprovision with the proper tool instead.

#### Removing the power-cycle requirement — two candidates, neither yet tested

Both routes exist because hardware re-evaluates `PEMPTY` at an option-byte
load as well as at power-on.

- **Per flash: trigger an option-byte load at the end of the programming
  command.** `-ob obl` is not a flag in STM32CubeProgrammer v2.22.0 — `-ob`
  takes `displ`, `unlockchip` or `OptByte=<value>`, and the help has no
  `launch`, `obl` or `reload`. The tool performs the load as part of *writing*
  an option byte, so the form to try is a write of an existing value, e.g.
  `-c port=SWD mode=UR -e all -w <image>.hex -ob nSWBOOT0=1`, with the caveat
  that a write of an unchanged value may be skipped and so launch nothing.
- **Once per board: disable the empty check.** `nSWBOOT0 = 0` with
  `nBOOT0 = 1` stops `PEMPTY` routing resets to the ROM bootloader at all
  (RM0394 2.6). This removes the requirement permanently instead of adding a
  step to every flash, which makes it the better fix if it holds — but it is
  an option-byte change to make deliberately, with its own verification, not a
  clause bolted onto a flashing command.

Establish which works at the next flash that needs a mass erase, and record the
result here. Until then, power-cycle.

### Steps

**1. Unpack the release archive** (`tar -xzf tag-firmware-fw-vX.Y.tar.gz`) on
the machine with the ST-LINK attached. No build tree, CMake configure or
toolchain is needed -- each `firmware/<Tag>/` directory is self-describing, with
`BitTag.elf` beside `BitTag-build-manifest.json`. `<release>` below is the
unpacked `firmware/` directory.

**2. Program, with the board's label:**

```sh
python3 <repo>/embedded/tools/flash_release.py <release>/BitTag \
    --label BitTag-017 \
    --json ~/tags/programmed.jsonl
```

This checks the image against the SHA-256 its manifest records and refuses to
program on a mismatch, prints the provenance it verified, then programs through
the same probe selection the CMake targets use. `--verify-only` checks without
programming. `--selector` picks an ST-LINK when several are attached; a
`STM32_PROGRAMMER_PROBE` set in the environment overrides it.

It does **not** check that the image belongs on the attached board, and cannot:
the programmer reports the MCU, not the board, and four of the five distributed
tags are `stm32l4xx`. A device-ID check would catch only a mix between
`IMUTagNandBmp581` and the other four -- 8 of the 20 wrong pairings -- and would
pass for `BitTag` onto a PresTag board while printing "verified", which is worse
than checking nothing. Boards are labelled by hand for this reason, and `--label`
records which one was claimed rather than verifying it.

The `--json` record is written whether or not programming succeeded, with
`programmed` saying which. A failed flash that left no row would be
indistinguishable from one that never happened, and the tag in hand would not be
running what the record implies.

**3. Confirm the tag runs and read back what it says:**

```sh
tag-test                 # the usual check that the hardware works
tag-info --json          # machine-readable, for the record
```

**4. Record the row** in the board database. The database is kept outside this
repository: every board is labelled, and its git hash and MCU unique ID are
recorded there before it goes to the field. That is the link from a physical
tag to what runs on it. Between the two files:

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
which commit it was built from, forever: `infoAck` returns `githash` because
`VERSION_HASH` is baked in, and the UUID it returns is the factory `UID_BASE`
register, read-only and untouched by flashing. It can never report which *build*
of that commit, because an image cannot contain its own hash; embedding the hash
changes the bytes being hashed. `build_time` is the commit date, not a compile
time, so it does not distinguish builds either
([decision 0010](../decisions/0010-build-image-is-determined-by-the-commit.md)).

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
   `tag_release_check.py --skip-build --target <Tag> --config <config>` (BitTag
   by hand, as above). Keep the output directory and write the
   release tag and image SHA-256 into it.
5. **Write the result into the release.** The release page is where someone
   decides whether an image may fly, so the answer belongs there and not only
   in a local directory. See *Publishing the qualification* below.
6. **Program the field tags** from the same release with `flash_release.py`,
   with `--label` and `--json`.
7. **Run `tag-test`** on each to confirm the hardware works, then `tag-info
   --json`, and enter the board database row.

Steps 4 and 6 are separate because they answer different questions.
Qualification is per image: one measurement clears the image every tag of that
type will receive. Programming is per tag, and each one needs its own row.

A target that is not flying this round does not need qualifying. A target that
is needs it again after any change to its image -- which, the build being
reproducible, means after any change to the commit it is built from.

### Publishing the qualification

A qualification that lives only on the bench cannot be acted on by anyone
deciding what to flash. CI publishes each image already stating that it has
**not** been bench-tested, so every release starts out honest and is corrected
as targets are cleared. Update the release as each one is qualified rather than
waiting for the set: a partially qualified release is the normal state, and
saying which targets are cleared is the whole point.

Two places, for two readers:

- **The release body** carries the verdict -- one row per distributed target,
  so the question "can I fly this image?" is answered without downloading
  anything. Targets that have not been qualified say so explicitly. `gh release
  edit` replaces the body rather than appending, so build the new text from the
  current body.
- **A release asset** carries the evidence: the session directory for each
  target, with the board label and UUID, the supply voltage, the image
  SHA-256 read back from the manifest that was flashed, and the logs.

```sh
gh release view  fw-vX.Y --json body --jq .body > notes.md
# edit notes.md: update the qualification table
gh release edit   fw-vX.Y --notes-file notes.md
gh release upload fw-vX.Y qualification-<target>-<date>.tar.gz
```

The table states the supply voltage with every current, because it is not the
same for every tag -- an unregulated target such as BitTag is measured at its
cell voltage, and a figure carries no meaning without it:

| Target | Qualified | Supply | Idle | Running | Finished |
| --- | --- | ---: | ---: | ---: | ---: |
| BitTag | `fw-v0.5`, 2026-10-03 | 2.496 V | 0.1224 uA | 0.508 uA @ 1 activity bit/s | 0.1214 uA |
| IMUTagNandBmp581 | not qualified from a release; figures from a power sweep of local build `b025e7ba`, 2026-10-03 | 3.693 V | 5.52 uA | 662 uA @ 400 Hz | not measured |

The BitTag row is from [its results log](../../embedded/tags/BitTag/design/power-results.md);
the IMUTagNandBmp581 figures are from the
[IMUTag power results](../../embedded/tags/families/IMUTag/design/power.md),
where idle is the mean of six readings through the sweep.

**Qualify the released image, not a local build at the same commit.** The two
are expected to be identical and usually are, but "expected to be" is what
qualification exists to replace; a row whose numbers came from a local build
must say so.

## 3. Releasing the host tools

Host tools release on their own `vX.Y[.Z]` tags, separate from the `fw-v*`
firmware tags, because they are validated differently: a host tool is qualified
by running it, and needs no bench.

### Why this one is half-manual

CI builds both packages and attaches the Windows one. The macOS one it builds
but cannot finish.

macOS refuses to launch an app whose signature it does not accept, and the
signature it accepts requires the Indiana University Developer ID certificate.
Putting that certificate and its password into a public repository's secrets
would make the private key reachable by anyone who can land a change to a
workflow, and by every third-party build step that runs beside it, so it stays
on the developer's machine. CI therefore builds the DMG ad-hoc signed and
attests where it came from, and the `sign-latest` target brings that build to
the key: it checks the attestation, re-signs every bundle with the same code
that signs a local build, and uploads the result to the draft. The reasoning is
in [decision 0024](../decisions/0024-release-macos-package-built-on-ci-signed-locally.md).

So `release.yml` opens the release as a **draft** with only the Windows ZIP.
The draft is the signal that the release is incomplete. It becomes publishable
once the Developer ID signed DMG is attached.

The same step notarizes the DMG and staples the ticket to it, so a user who
downloads it gets one "downloaded from the internet" confirmation per app and
nothing else. The notary credentials stay on that Mac too
([decision 0025](../decisions/0025-release-macos-package-notarized-locally.md)).
Releases up to v3.0.1 were not notarized; their users still approve each app
by hand, as
[Installing a macOS Release](../../README.md#installing-a-macos-release)
describes.

v3.1 was the first release built by CI, signed this way and notarized. Every
app in it was downloaded from the draft, opened on macOS 26 and connected to a
tag before it was published (decision 0025).

### Steps

Run the signing step on the Mac holding the Developer ID certificate, with `gh`
logged in.

**Once per Mac: store the notary credentials.** Notarizing needs an Apple ID
on the Indiana University developer team and an app-specific password for it,
made at <https://account.apple.com> under Sign-In and Security ->
App-Specific Passwords. Store them in the keychain under the profile name the
script uses:

```
xcrun notarytool store-credentials tag-notary --apple-id <apple-id> --team-id 5J69S77A7G
```

It prompts for the password and checks it with Apple. `sign-latest` stops
before downloading anything if the profile is missing; `MACOS_NOTARY_PROFILE`
names a different one.

**1. Push the commits.** Not the tag -- the commits. CI builds whatever the tag
points at, and a tag pointing at a commit nobody else has is a release nobody
can reproduce. Check with `git branch -r --contains HEAD`.

**2. Tag and push the tag.** The package is named from the highest `vX.Y` tag
reachable from the commit, so release from a commit where the new tag is the
highest.

```
git tag -a v3.1 -m "Host tools v3.1"
git push origin refs/tags/v3.1
gh run watch
gh release view v3.1
```

A draft, one asset, the Windows `.zip`, no `.dmg`. A DMG appearing there means
the artifact filter in `release.yml` stopped working.

**3. Sign, notarize and upload the macOS package**, from any configured macOS
build tree:

```
cmake --build <build-dir> --target sign-latest
```

It builds nothing; it runs `host/tools/sign-ci-macos.sh` with the identity and
entitlements that tree is configured with. To sign a tag other than the newest,
run the script directly: `host/tools/sign-ci-macos.sh v3.1`.

| It does | Because |
| --- | --- |
| checks the identity is in the keychain, and is not ad-hoc | a missing identity otherwise fails partway through signing |
| checks the notary profile works | missing credentials otherwise fail after minutes of signing |
| takes the newest successful tag-push run of `release.yml` | that is the release being cut |
| checks the tag still points at the commit the run built | a moved tag would attach a package built from another commit |
| verifies the DMG's provenance attestation against `release.yml`, that commit and that tag | what gets the Developer ID signature is provably this repository's build, not a swapped file |
| copies the image out and re-signs every bundle with `cmake/MacosCodesignBundle.cmake` | the same code signs a local build, so the signatures match |
| makes a new HFS+/UDZO image and signs it | the image carries the identity the ticket is stapled under |
| submits the image to Apple's notary service and waits; on anything but `Accepted` it prints the notary log and stops | the log names each rejected file and why |
| staples the ticket to the image | Gatekeeper can then accept the image without asking Apple |
| mounts the image and verifies each app, the stapled ticket and that Gatekeeper accepts all of it as notarized | what matters is the signature a user receives |
| prints the DMG path, its SHA-256 and the run it came from | the hash identifies the image that was tested |
| uploads it to the draft release, replacing any earlier upload | it never changes a published release |

The signed DMG is also kept in `<build-dir>/signed-release/`.

Expect thirteen `ok` lines -- five Qt apps, `dataprocessing`, and seven command
line tools. A count that is not thirteen means the install set changed; check
that against `host_cli_install_targets` before shipping.

Each app's `ok` also means Gatekeeper accepted it as `source=Notarized
Developer ID`, and two more lines follow: `stapled ticket ok` and `image
assessment ok`. Signing and timestamping several hundred items, then waiting
for Apple, takes several minutes and needs the network. `--no-notarize` skips
notarization, and the check then expects only `source=Developer ID`.

That assessment is what Gatekeeper would decide. It does not show that the
apps run when launched for the first time from a quarantined download; only
the download test in step 4 does.

**4. Install it from the release page on a Mac that has never seen the build,**
and follow
[Installing a macOS Release](../../README.md#installing-a-macos-release) as
written. Launch every app, including `qtcalibrate`'s 3D view. This is the only
step that tests what a user experiences, because it is the only copy that is
quarantined. Everything before it tests the build.

**5. Publish the draft.**

### Building and signing entirely locally

`host/tools/release-macos.sh` builds the macOS package on this Mac instead of
taking CI's, and signs it with the same code. Use it when the CI build cannot
ship, or when CI is unavailable. It does not notarize, so a package from it
ships with the per-app approval described for v3.0.1 and earlier.

```
host/tools/release-macos.sh v3.1 --no-push
git push origin refs/tags/v3.1
gh release upload v3.1 ~/Build/tag-designs/software-vcpkg-release/Ultralight-tags-v3.1.dmg
```

`--no-push` is the recommended order. The tag is created locally, the image is
built and checked, and nothing has been announced. If anything fails, `git tag
-d v3.1` and the attempt leaves no trace. Without it the script pushes first,
which starts CI and opens a draft release before anything has been verified.
The script refuses a dirty tree, creates the annotated tag before configuring
(the DMG is named from the tag at configure time), checks that CMake resolved
that tag and not a higher one, builds, packages, and verifies the mounted DMG
with `host/tools/verify-macos-dmg.sh`, the same check `sign-latest` runs.
Then continue from step 4 above.

### If something goes wrong

`sign-latest` stops before uploading anything if a check fails:

- **No valid provenance attestation.** The run predates the attestation step,
  or the file is not what `release.yml` built. For a pre-attestation run only,
  `host/tools/sign-ci-macos.sh vX.Y --no-attestation` signs it without that
  assurance.
- **The artifact has expired.** Workflow artifacts are kept 14 days. Re-run the
  tag's workflow (`gh run rerun <run-id>`) and sign the new run.
- **There is no release yet.** The release job has not finished; wait for it
  and run the target again.
- **The release is already published.** It is left alone; the script prints the
  `gh release upload --clobber` command for a deliberate replacement.

With `release-macos.sh --no-push`, almost nothing can go wrong: delete the
local tag and start over.

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
