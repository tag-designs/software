# Tag Firmware Build Reproducibility

Status: implemented and verified. A firmware build of the distributed tags
needs the ARM toolchain, `make`, `cmake` and `python3` -- no `fmpp`, no Java
runtime, no nanopb generator, no protobuf. Every image carries a manifest keyed
on its own SHA-256, and the conditions that would quietly make a build
unreproducible are detected at configure time. CI has built all five distributed
tags on a runner and every hash its manifests record was verified against the
bytes shipped. [What was not done](#what-was-not-done) lists the gaps, which are
real and deliberate -- the largest being that nothing yet collects a manifest
from a bench-built image, which is how most flown firmware is flashed.

## Purpose and scope

Two questions this has to answer:

- **Forward.** Given a commit and a recorded environment, can the same image be
  built again?
- **Backward.** Given an image recovered from a tag, can it be established
  exactly what produced it?

The scope is tag firmware. Host tools are excluded: they release on a different
clock, their dependencies are pinned by the vcpkg baseline, and no one debugs a
returned bird tag against a copy of `qtmonitor`.

Neither question is academic here. `AGENTS.md` records that a test image differs
from a shipping one by a `-D` that leaves no trace in the git hash, and that
STM32U375 Standby entry depends on where code lands in the image -- the same
source can sleep or stall depending on unrelated changes. "Same commit" is not
the same claim as "same image", and this document is about closing the distance
between them.

Data recovery from a returned tag is covered separately in
[Field Data Extraction](field-data-extraction.md); the two meet at the image
hash.

### Which tags are in scope

Not every tag in the tree deserves this. Most are development prototypes that
were built once to try a sensor or a flash part; they will never be deployed and
never returned from the field, so nothing is gained by promising that a build
from a future checkout reproduces them byte for byte, and something is lost --
every additional variant is more generated output to commit, more to keep fresh,
and more to break a build over.

The distinction the tree already wanted is the one CMake supplies: **the actively
deployed tags are the ones that install.** `add_embedded_target` takes a
`DISTRIBUTE` keyword; marked targets install their firmware artifacts and are
recorded in the global property `ULTRALIGHT_DISTRIBUTED_TAGS`. That gives three
tiers:

| tier | how it is expressed | obligation |
| --- | --- | --- |
| retired | commented out of `embedded/tags/CMakeLists.txt` | none |
| prototype | added, but without `DISTRIBUTE` | must compile |
| distributed | `add_embedded_target(... DISTRIBUTE)` | reproducible: committed generated sources, pinned tool versions, version agreement enforced, firmware in the release |

The distributed set is `BitTag`, `CompassTagAT25`, `IMUTagNandBmp581`, `PresTag`
and `UIUCTag` -- five of the fourteen configured. `BitTag-legacy` was marked and
then unmarked: it does not currently build, which is exactly the kind of thing
the tier is meant to keep out of a release.

**Nothing downstream is declared twice.** The proto-c variants and the boards
that need the same treatment are *derived* from the tag markings:

- `add_embedded_target` already knows each tag's proto target, so the
  distributed proto set falls out of it: `bittag_proto`, `compasstag_proto`,
  `imutag_proto`, `prestag_proto`, `uiuctag_proto`.
- A tag names its board in `project.mk` as `include $(BOARDDIR)/<board>/board.mk`,
  so the distributed board set is read from there: `BitTagv6`, `CompassTagv1`,
  `IMUTagNandv2`, `PresTagv3`, `UIUCTag`.

Everything that follows applies to that derived set. Prototypes keep generating
their sources at build time and are not held to the freshness check;
`REPRODUCIBLE_BUILD=ON` escalates to an error only for what ships. A prototype
that later goes into the field is promoted by adding one keyword to a marker the
build already acts on, rather than by editing a list in a document.

## The model as built

### What a build consumes

**1. Present in the repository.** Tag, board and common firmware sources,
`project.mk`, the `.proto` definitions, per-board customizations, per-variant
`*.override.options`. Fully described by the commit.

**2. Generated from files in the repository.** For the distributed set these are
now committed, so they are also category 1 -- described by the commit, and not
rebuilt unless their recorded inputs change:

| Output | From | Committed for the distributed set? |
| --- | --- | --- |
| `board.{c,h,mk}`, `board_standby.h` | `fmpp` + ChibiOS `.ftl` templates, via `generate_board_chcfg.py` | yes, in `embedded/boards/<board>/generated/` |
| `tag.pb.{c,h}`, `tagdata.pb.{c,h}` | `nanopb_generator` | yes, in `embedded/proto-c/<variant>-proto-c/generated/` |
| `default_config.c` | `config-gen` from `default-config.json` | yes, beside the `.pb.*` |
| `tag.options`, `tagdata.options` | `CombineFiles.cmake` over the default and per-variant options | no -- pure CMake, no external tool, regenerated cheaply |
| `cfg/board.chcfg`, `cfg/board.fmpp` | `generate_board_chcfg.py`, `configure_file` | no -- intermediates, only produced when regenerating |

**3. Shared external code.** ChibiOS and nanopb. Both are compiled into the
image, and neither is described by the firmware sources.

- **ChibiOS** is a submodule. The superproject records a SHA, and configure now
  checks that the submodule is at that SHA, is clean, and that the SHA is on the
  branch `.gitmodules` says it tracks (`stable_21.11.x`). The last of those
  catches something none of the others can: a pointer moved to `master` and
  committed *is* the recorded commit, so nothing else would notice.
- **nanopb plays two roles.** The *runtime* -- `pb.h`, `pb_common.c`,
  `pb_encode.c`, `pb_decode.c`, compiled into every image -- is vendored at
  `embedded/thirdparty/nanopb-0.4.9.1/`, with `CHECKSUMS.txt` and a README
  recording the upstream tag and commit. The *generator* stays outside the
  repository at `NANOPB_SRC_ROOT_FOLDER`, and is needed only to regenerate.
  Configure compares three things that must agree: the version in the vendored
  directory's name, the `NANOPB_VERSION` in its `pb.h`, and the version the
  generator reports.

### What is committed, and how staleness is detected

Each committed `generated/` directory carries an `inputs.sha256` manifest: the
SHA-256 of every input the generators consume, plus the pinned tool version.

| | proto-c variants | boards |
| --- | --- | --- |
| Inputs hashed | both `.proto` files, both halves of each options file, `default-config.json`, `config-gen.cc`, `CombineFiles.cmake` | `board-customizations.json`, `generate_board_chcfg.py`, `board.fmpp.in`, the ChibiOS pin XML, `board.{c,h,mk}.ftl`, and every file in ChibiOS `tools/ftl/libs/` |
| Tool line | `nanopb <version>` | `chibios-templates <processor>` |

Two decisions in there are worth keeping in mind:

- **Timestamps are not used.** Git does not preserve mtimes, so a fresh clone
  would look stale and a checkout could look fresh when it is not.
- **ChibiOS inputs are recorded under a fixed `<chibios>` label**, not a relative
  path, because `CHIBIOS_DIR` may point outside the repository and `../../..` in
  a manifest would differ between machines while meaning the same thing. Their
  contents are still hashed, so a submodule bump still shows up.
- **`fmpp`'s own version is deliberately absent** from the board manifests. A
  manifest has to compare equal across machines, and a tool version reported on
  one machine and not another would cause false staleness. What a given machine
  had belongs in the per-image build manifest instead.

### The two switches

`REGENERATE_SOURCES` decides whether committed generated sources may be rebuilt:

| value | behaviour |
| --- | --- |
| `AUTO` (default) | regenerate exactly when the recorded inputs no longer match |
| `ON` | always regenerate, and write the result back into `generated/` |
| `OFF` | never regenerate; a stale committed source is reported, not fixed |

`REPRODUCIBLE_BUILD` decides how loudly anything unreproducible is reported.
`OFF` (the default) warns, so day-to-day work is not obstructed. `ON` turns every
such warning into a configure error, which is how CI and release qualification
build.

Under `OFF`, targets that commit nothing -- prototypes and bases -- are
configurable but unbuildable: asking for one fails with a message saying to use
`AUTO`. A release build configures the whole tree and builds only the
distributed targets, so they have to be configurable; failing at the moment one
is actually requested says the same thing where it matters.

### What is checked at configure time

Each of these goes through `reproducibility_problem()` in
`cmake/ReproducibilityChecks.cmake`, so `REPRODUCIBLE_BUILD=ON` makes it an
error:

| Condition | How it is detected |
| --- | --- |
| Working tree dirty | `git status --porcelain`, modified tracked files only |
| Vendored nanopb runtime edited | the same, scoped to `embedded/thirdparty` |
| Submodule uninitialized, off its recorded commit, or unmerged | `git submodule status` leading `-`, `+`, `U` |
| Submodule clean but dirty inside | `git status --porcelain` within it |
| Submodule not on the branch `.gitmodules` tracks | `git merge-base --is-ancestor HEAD origin/<branch>` |
| Toolchain other than `ARM_TOOLCHAIN_VERSION` | `arm-none-eabi-gcc -dumpversion` |
| Committed generated sources stale | manifest comparison, per variant and per board |
| Generator version disagrees with the vendored runtime | `nanopb_generator --version` |
| Distributed target that commits nothing, or committed files nothing distributes | the derived sets, compared |
| HEAD not at a tag | `git describe --exact-match`, strict mode only |

Untracked files are not treated as dirty: nothing compiles them, and warning
about scratch files would make the warning routine and so ignorable.

### What is recorded

**Beside each image**, `<name>-build-manifest.json`: the commit and `describe`
output, whether the tree was dirty, the ChibiOS commit, its `describe` and the
branch it tracks, whether it was dirty, the toolchain path and version, the
nanopb runtime and generator versions, both build modes, whether that variant's
sources are committed, and the SHA-256 and size of the `.elf`, `.bin` and `.hex`.
Git facts are read when the manifest is written rather than at configure time,
because the tree can change in between and the manifest should describe the image
that exists. A missing artifact is recorded as `null` rather than omitted, so a
truncated build is visible rather than merely unremarkable.

One field is emptier than it looks. `nanopb_generator` records the version the
generator reported at configure time, and under `REGENERATE_SOURCES=OFF` no
generator is looked for, so a CI manifest leaves it blank. That is correct --
nothing generated anything, so there is no version to record -- but it means a
CI manifest carries slightly less than a bench-built one, and the absence should
not be read as a missing generator having been used.

**In the image**, `version.h` defines `VERSION_HASH`, `GIT_SHA`, `GIT_DATE`,
`GIT_COMMIT_SUBJECT`, `GIT_REPO`, and now also `GIT_DIRTY`, `GIT_DIRTY_STR`,
`CHIBIOS_SHA` and `NANOPB_RUNTIME_VERSION`. The last four are macros that cost
nothing unless referenced, and nothing references them yet -- see
[What was not done](#what-was-not-done).

### What a build actually needs

| Tool | Needed for a distributed-tag firmware build? |
| --- | --- |
| `arm-none-eabi-gcc` 14.2.1 and binutils | always; version is pinned and checked |
| `make`, `cmake` | always |
| `python3` | only to regenerate a board |
| `fmpp` and a Java runtime | only to regenerate a board |
| `nanopb_generator` | only to regenerate protocol sources |
| `config-gen` and host protobuf | only to regenerate protocol sources |

With `REGENERATE_SOURCES=OFF` none of the optional rows are looked for at all:
`config-gen` is not built, protobuf is not searched for, and `fmpp` and `python3`
are looked up leniently and demanded only where a board actually regenerates.

### CI

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `embedded-reproducibility.yml` | every push and pull request | runs `cmake/CheckGeneratedSourcesFresh.cmake` over the committed proto variants and boards |
| `release-firmware.yml` | `fw-vX.Y` tags, and `workflow_dispatch` | installs the pinned toolchain, configures with `-DREGENERATE_SOURCES=OFF -DREPRODUCIBLE_BUILD=ON`, builds `distributed_firmware`, publishes images with their manifests |
| `release.yml` | `vX.Y` tags | host tools only, unchanged |

The freshness check reuses `InputManifest.cmake`, so it cannot disagree with what
the build writes, and it reads the `.proto` list out of `proto/CMakeLists.txt`
and each board's `PROCESSOR` out of its own `CMakeLists.txt` rather than
repeating either. It needs CMake and a checkout -- no ARM toolchain, no `fmpp`,
no generator. A check that needed the generator in order to prove the generator
is unnecessary would be self-defeating. ChibiOS is checked out because the board
templates are inputs, and `REQUIRE_CHIBIOS=ON` makes a missing submodule a
failure, since in CI a skipped check looks exactly like a passing one.

`release-firmware.yml` has been run to completion on a runner via
`workflow_dispatch`. It produced all five distributed tags, seven files each,
with `reproducible_mode: true`, `regenerate_sources: OFF`, a clean tree, the
pinned toolchain at 14.2.1 and the expected ChibiOS commit. Every SHA-256 and
size a manifest records was re-checked against the bytes actually shipped: 15 of
15 matched. That is the backward question working -- bytes recovered from a tag
can be matched to an archived build with nothing to trust but the bytes.

Getting there took one real failure, worth recording because it is the failure
mode the pin exists for. The first run rejected the toolchain: the pinned hash
belonged to a different file in the same release, because Arm ships `x86_64`,
`aarch64` and `darwin-arm64` builds of 14.2.rel1 under near-identical names and
the checksum taken was the one a Mac is offered. The download was fine and the
pin was wrong. The workflow now prints the downloaded file's size, type and
computed hash alongside the pinned one, and the checksum Arm publishes beside
the file -- printed for comparison and never acted on, since a checksum served
by the same host as the file proves nothing about the file.

Firmware and host tools use separate tag namespaces because they are validated
differently. A host tool release is exercised by running it; a firmware release
has to be bench-tested for power before it can fly. Coupling them would either
demand that validation every time a host tool ships, or invite it to be skipped.

## Maintenance

Most of this is one keyword or one directory. The machinery is built so that
forgetting a step produces a message naming the step, not a silently wrong
image.

### Quick reference

| Change | What to do |
| --- | --- |
| Add a prototype tag | `add_subdirectory` in `embedded/tags/CMakeLists.txt`; nothing else |
| Promote a tag to distributed | add `DISTRIBUTE`; create the `generated/` directories its proto variant and board need; regenerate and commit |
| Stop distributing a tag | remove `DISTRIBUTE`; delete the `generated/` directories nothing else uses |
| Add a board | as for any board; only create `generated/` if a distributed tag uses it |
| Change a `.proto` or an options file | build normally under `AUTO`, then commit what changed under `generated/` |
| Bump ChibiOS | update the submodule, rebuild the boards under `AUTO`, commit the regenerated board files |
| Bump nanopb | replace the vendored directory, point `NANOPB_ROOT` at the matching generator, regenerate, commit |
| Change the ARM toolchain | update `ARM_TOOLCHAIN_VERSION` in `CMakeLists.txt` and the URL and SHA-256 in `release-firmware.yml` |

### Promoting a tag to distributed

1. Add `DISTRIBUTE` to its `add_embedded_target` call.
2. Configure. It will tell you what is missing: the proto variant and the board
   it uses are named as "used by tags marked DISTRIBUTE but do not commit their
   generated files".
3. Create those directories -- `embedded/proto-c/<variant>-proto-c/generated/`
   and `embedded/boards/<board>/generated/`. Their existence is what declares
   that the target commits its sources; git cannot track an empty directory, so
   this step is a local `mkdir` whose effect is committed along with the files.
4. Configure with `-DREGENERATE_SOURCES=ON` and build the proto and board
   targets. This needs `fmpp`, a Java runtime, and the pinned nanopb generator.
5. Commit what landed. A plain `AUTO` configure should then be silent.

### Stopping distribution of a tag

Remove `DISTRIBUTE`, then delete the `generated/` directories that nothing else
uses. Configure warns about generated files no distributed tag uses, naming them
as leftovers -- leaving them would be committing output nobody checks. Note that
a board or proto variant may still be used by another distributed tag, in which
case it stays.

### Changing a `.proto`, an options file, or a board's customizations

Build normally. `AUTO` notices the recorded inputs no longer match and
regenerates, writing back into `generated/`. Commit the result along with the
change -- the CI freshness check fails on a pull request that changes an input
without the regenerated output, naming the variant or board.

This is the case where the generators are needed, so a developer changing a
protocol or a pin map still needs `fmpp`, a Java runtime and the nanopb
generator installed. A developer who only builds firmware does not.

### Bumping ChibiOS

The board templates live in ChibiOS, so a submodule bump makes every committed
board stale. That is intended: the templates are inputs like any other.

1. Update the submodule. It must land on `stable_21.11.x`, or configure will say
   so -- that check exists because a pointer moved to `master` and committed is
   indistinguishable, to every other check, from the right commit.
2. Build the board targets under `AUTO`. Each regenerates.
3. Commit the regenerated board files. **Read the diff**: board files are
   deliberately not marked `linguist-generated`, precisely so that what a
   ChibiOS bump changes in `board.c` is visible in review.

### Bumping nanopb

The runtime and the generator must move together, and configure enforces it.

1. Replace `embedded/thirdparty/nanopb-<old>/` with `nanopb-<new>/`, named for
   the exact version. The directory name is the pin; configure derives the
   version from it and compares against the `NANOPB_VERSION` in the vendored
   `pb.h`. Exactly one such directory must exist -- a second one left behind by
   a half-finished upgrade is a configure error rather than a coin flip.
2. Update `CHECKSUMS.txt` and the README recording the upstream tag and commit.
3. Point `NANOPB_ROOT` at a matching generator distribution.
4. Regenerate under `ON` and commit. The generated headers carry the generator
   version in a comment, so the diff shows the bump.

### Changing the ARM toolchain

`ARM_TOOLCHAIN_VERSION` in `CMakeLists.txt` is what every build checks against;
`release-firmware.yml` pins the tarball URL and its SHA-256 for CI. Both must
move together, or CI installs one version and the build rejects it.

The version compared is what `arm-none-eabi-gcc -dumpversion` reports, which for
Arm's `14.2.rel1` release is `14.2.1`. Setting `ARM_TOOLCHAIN_VERSION` to an
empty string records the version without checking it, for anyone who needs to
build on something else deliberately.

**A toolchain change is a firmware change.** A different compiler produces a
different image from the same sources, and on the STM32U375 that can change
whether Standby is reached. Bump it deliberately and re-qualify.

## Identity: the image hash

`VERSION_HASH` and `SHAStr` name a commit. They do not identify an image: a `-D`
leaves no trace in the git hash, and `git rev-parse HEAD` ignores uncommitted
changes. Two materially different binaries can report the same SHA.

**Key the archive on the SHA-256 of the image.** A capture that includes the
internal flash region contains the bytes, so hashing them identifies the build
with no metadata to trust and no firmware change required. The embedded git
identity stays useful as a human-readable label and for live readback over the
monitor, but it is not the key.

This is observable rather than theoretical: two builds of consecutive commits
produced `.elf`, `.bin` and `.hex` of *identical size* and entirely different
hashes, because `SHAStr` carries the commit into the image. The converse is the
case that matters -- same commit, different bytes -- and the manifest's hash is
what distinguishes it.

## The archive

Per image: the ELF with DWARF, the `.map`, the `.bin`, `project.mk`, the linker
script, `compile_commands.json`, the `build-manifest.json`, and -- where one
exists -- the external loader used to read that board's flash. Single-digit
megabytes. The firmware release workflow attaches the images and manifests
automatically; the rest is still manual.

**CI does not qualify an image.** A green build says the sources compile and the
provenance is recorded. It says nothing about power behaviour. `AGENTS.md`
records a 240x idle-current regression that passed a clean build and a hardware
feature test, and that Standby entry depends on where code lands in the image, so
a change with no visible effect on the source can change whether a tag sleeps.
`embedded/tools/tag_release_check.py` and a Joulescope measurement are what
produce a release; CI produces candidates. A manifest accompanying an untested
image is not a warrant.

Archive artifacts rather than planning to rebuild them. In three years the
toolchain will not install cleanly; the recorded versions make a rebuild
*possible*, but nothing downstream should depend on it. Reproducibility is the
cheaper of two insurance policies, not a substitute for the other.

## What was not done

### The dirty flag is not carried into the image

`version.h` defines `GIT_DIRTY`, `GIT_DIRTY_STR`, `CHIBIOS_SHA` and
`NANOPB_RUNTIME_VERSION`, but no firmware source references them, so none of it
reaches the image. A tag returned from the field still cannot say "the tree I was
built from had uncommitted changes" over the monitor; only the build manifest
says that, and only if the manifest was kept.

This was left deliberately. Putting a new string into the image shifts its
layout, and Standby entry on the STM32U375 depends on where code lands, so
wiring these into the monitor is a firmware change that deserves its own commit
and its own bench measurement -- not a side effect of recording provenance.

### Determinism, verified within and across machines

`monitor.c` used to embed `__DATE__ " : " __TIME__`, which made byte-identical
rebuilds impossible by construction: the same commit produced different bytes
every time, however carefully the inputs were pinned.

It now carries `GIT_DATE` instead -- the commit's author date, from
`version.cmake`, in `iso-strict` form so that it does not vary with the
builder's timezone. The field still reports a date, which the tags rely on; it
reports when the source was committed rather than when someone happened to
compile it, which is the more useful answer from a tag in hand anyway. It is
always exactly 25 characters against a 30-byte field.

**The measurement has been taken, and it passes.** A distributed tag built twice
from a clean tree, into two different build directories, produced a
byte-identical `.bin`. The differing build paths make that stronger than it
sounds: anything leaking a build location into the image -- `__FILE__`, a debug
path, a temporary name -- would have shown as a difference, and none did.
`__DATE__`/`__TIME__` now appear nowhere that compiles into a distributed tag,
not in ChibiOS' HAL, RT or common code, and only in `BitTag-legacy`, a prototype
that does not build.

This closes the loop the document opened with. A commit determines its image, so
the SHA-256 recorded in a board database can be re-derived from the commit alone
by anyone with the repository -- the forward question answering the backward
one, and no longer dependent on the archive surviving.

### Across machines: verified, after three clone properties were removed

That comparison has now been made, between a CI image from `fw-v0.0.1` and a
local rebuild of the same commit -- Linux against macOS, Arm's tarball against
Homebrew's build of the same 14.2.1. The images are the same size and **63 of
33416 bytes differ**, which is the useful part of the result: had the toolchains
disagreed, the sizes would have moved and the differences would be everywhere.
They are not. The compiled code is identical. Every differing byte is a string
git supplied, or a pointer displaced by one:

| | CI | local | cause |
| --- | --- | --- | --- |
| `GIT_REPO` | `https://github.com/...` | `git@github.com:...` | how the clone was made |
| `VERSION_HASH` | `a3f596d0` | `a3f596d` | `--short` picks the shortest unambiguous abbreviation, which depends on the object count |
| `GIT_DATE` | `...:30Z` | `...:30+00:00` | git renders UTC differently between versions, even under `iso-strict` |

All three are now pinned in `version.cmake`: `--short=8`, an explicit strftime
format under `TZ=UTC` rather than a format git chooses, and the remote reduced
to `host/owner/repo` so that SSH and HTTPS clones agree.

**That closed it.** At `fw-v0.0.2`, a CI image and a local rebuild of the same
commit are byte-identical: `24d009da...` on a Linux runner using Arm's tarball
and on macOS using Homebrew's build of 14.2.1, twenty-four seconds apart. The
two build manifests still record different remotes -- `https://github.com/...`
against `git@github.com:...` -- which is what proves they are genuinely
different clones on different machines rather than the same artifact compared
with itself.

That difference belonging in the manifest and not in the image is the whole
distinction: the manifest describes a build, the image describes a commit.

The lesson generalizes past these three. Anything derived from the *clone*
rather than the *commit* is a reproducibility hazard, and git's conveniences --
abbreviation, date rendering, remote URLs -- are all clone properties wearing
commit clothing.

Nor has every distributed tag been checked -- one was.

### The old board generation path is untouched

`generate_board_files` -- the static flow that renders `cfg/*.ftl` from a
checked-in `board.chcfg` -- has none of this: no manifest, no committed output,
no staleness check. Four boards use it: `TagSteval`,
`bittag-base-jlcpcb-v2`, `bittag-base-jlcpcb-v3` and
`tag-breakout-base-jlcpcb32-v1`. None is behind a distributed tag, which is why
it was left alone.

If one ever is, the consistency check does notice: the board would appear in the
derived distributed set but never in `ULTRALIGHT_BOARD_COMMITTED`, which only
`generate_configured_board_files` appends to, so configure would report it as a
distributed board that does not commit its generated files. The message would
then be misleading, because the remedy it suggests -- create a `generated/`
directory and regenerate -- does nothing on the old path. The right response is
to port the board to `generate_configured_board_files` first.

### `fmpp` is not pinned

The board manifests hash the ChibiOS templates but not the renderer. A different
`fmpp` could in principle render the same templates differently and the manifests
would not notice. Pinning it was rejected because a version string reported on
one machine and not another would cause false staleness across the group, which
would be worse than the risk. The per-image build manifest does not record it
either -- that would be a small, easy addition.

### Programming a field tag

`embedded/tools/flash_release.py` programs a tag from a released artifact,
verifying the image against the SHA-256 its manifest records and refusing on a
mismatch. It needs no build tree: a release directory is self-describing, so it
runs on whatever machine has the ST-LINK attached.

That closes the gap where the image that flies was never the image that was
archived -- `<Tag>-download` programs a build tree, which is right for
development and wrong for the field.

It does not verify that the image belongs on the board attached. It cannot: the
programmer reports the MCU, not the board, and four of the five distributed tags
are `stm32l4xx`. A device-ID check would catch only a mix between
`IMUTagNandBmp581` and the other four -- 8 of the 20 wrong pairings -- and would
pass for `BitTag` onto a PresTag board while printing "verified", which is worse
than checking nothing. Choosing the right release directory is the operator's,
and boards are labelled for that reason.

**What links a physical tag to what is running on it is a database, kept
outside this repository.** Every board is labelled, and its git hash and MCU
unique ID are recorded there before it goes to the field. That is the backward
link, and it predates all of this.

**The image hash can only be captured at programming time.** A tag reports its
commit forever -- `infoAck` returns `githash` because `VERSION_HASH` is baked
in, and the UUID it returns is the factory `UID_BASE` register, read-only and
untouched by flashing. But an image cannot contain its own SHA-256: embedding
the hash changes the bytes being hashed. No better firmware fixes that; it is a
property of hashing, not a gap in the protocol.

`tag-info` returns `build_time`, but that is now the commit date rather than a
compile time, so it says nothing about which build a tag is running -- by
design. It used to distinguish builds to the second, which sounds useful and was
not: `__TIME__` recorded when `monitor.c` was compiled rather than when the image
was linked, so an incremental rebuild could change the image and leave the
timestamp untouched. It was traded for the possibility of two builds of a commit
being identical, after which there is nothing to distinguish.

The refinement this work argues for is therefore one field, recorded at the one
moment it is available: **the SHA-256 of the image, alongside the git hash.**
Until the build is byte-reproducible, that hash is the only thing that
distinguishes two builds of a commit with certainty. The whole premise here is that a git hash does
not identify an image -- a `-D`, an uncommitted change or a different compiler
leaves it untouched -- so a database keyed on it cannot distinguish two builds
that differ in ways that matter. Every release artifact carries that SHA-256 in
its manifest, and `flash_release.py` prints it before programming.

### Bench-built images have nowhere to put their manifests

CI attaches manifests to the release automatically. An image flashed from a
developer's bench produces a manifest in the build tree and nothing collects it.
There is no archive location and no naming convention. A tag returned in two
years is most likely to have been flashed from a bench, so this is where the
provenance of a returned image is thinnest -- the board database will name a
commit, and the manifest that said what that commit actually produced was left
in a build tree.

### Nothing records whether an image was qualified

The manifest says what was built. It has no field for whether
`tag_release_check.py` and a Joulescope measurement ever passed against that
image hash. The board database records what went onto each tag, but not whether
that image had been qualified, so "was this cleared to fly" is still answered
from memory rather than from a record.


### Host tools are still out of scope

Argued above and unchanged, but the `dataprocessing` path also produces records
that outlive their build, and nothing here applies to them.

## Settled: pinning does not shift the generated output

The worry was that fixing on 0.4.9.1 would shift the `.pb.*` files away from what
built the firmware now in the field, making the first commit of generated sources
a silent behavioural change.

It does not. Both generators were run over six proto variants (the five
distributed ones and `bittag-legacy`, distributed at the time of the test) from
the repository's own `.proto` files and `CombineFiles.cmake`-combined options,
and all 24 outputs compared:

- **0.4.8 vs 0.4.9.1** -- identical but for the `/* Generated by nanopb-... */`
  comment and some trailing blank lines. No declaration, field descriptor, size
  macro or `PB_BIND` entry differs in any of the 24 files compared.
- **0.4.8 vs the tree's actual old generator** (`1f0c2e1`, the
  `0.4.8-11-g1f0c2e1` snapshot) -- the only code change to `nanopb_generator.py`
  across those eleven commits is inside `fields_declaration_cpp_lookup`, emitted
  solely under `--cpp-descriptors`, which this project does not pass.

Moving to 0.4.9.1 from that snapshot also changes no runtime behaviour:
`pb_common.o` and `pb_encode.o` are byte-identical when cross-compiled for
`cortex-m4` and `cortex-m33`, and the sole semantic difference -- a leak fix in
`pb_decode_ex` under `PB_ENABLE_MALLOC` -- is in a function garbage-collected out
of the linked images, which call plain `pb_decode`.

## Settled: generator output does not depend on the platform

For nanopb 0.4.9.1, the macOS x86 release binary and the Linux PyPI wheel produce
**byte-identical** output: all 24 files across the six variants tested compared
equal, `.pb.c` and `.pb.h` alike, line endings included. The two runs also used
different include-path spellings, so the output is not sensitive to that either.

A regeneration on a Mac can therefore be committed directly; there is no need to
make Linux authoritative or to route protocol changes through a particular
machine. This is what makes the CI freshness check meaningful -- had it not held,
the check would have failed for everyone not building on the blessed platform.

## Open questions

- **Move to nanopb 0.4.9.2?** 0.4.9.1 is pinned because it is what already
  generated the tree's output. The newer LTS bugfix release is a change worth
  making deliberately and separately.
- **Where do bench-built manifests live**, and what links a qualification result
  to an image hash? See [What was not done](#what-was-not-done).
- **Do host tools need any of this?** Argued out of scope, but the
  `dataprocessing` path also produces records that outlive their build.

## Appendix: how nanopb was found disagreeing with itself

Worth keeping, because it is the concrete failure this document was written
against.

`NANOPB_SRC_ROOT_FOLDER` originally supplied both the include path for the
*runtime* -- `pb_encode.c`, `pb_decode.c`, `pb_common.c`, `pb.h`, compiled into
every shipped image -- and the `generator-bin` hint for the *generator* that
produces the `.pb.*` sources. The tree was listed in `.gitignore` and supplied
per developer, so neither role had a recorded version.

The tree actually in use was a git clone at `nanopb-0.4.8-11-g1f0c2e1`, an
untagged master snapshot whose `pb.h` self-reports `0.4.9-dev`, while the
generator beside it reported `0.4.9.1`. Runtime and generated code disagreed in
every image built on that machine. `PB_PROTO_HEADER_VERSION` is 40 for both, so
the one safety net nanopb provides could not see it: it catches 0.3 against 0.4,
not 0.4.7 against 0.4.9.1.
