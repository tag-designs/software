---
type: design
status: current
summary: The reproducible firmware build as implemented: distributed-tag scope, committed generated sources, per-image SHA-256 manifests, configure-time checks, CI, and how to maintain it.
---

# Tag Firmware Build Reproducibility

A firmware build of the distributed tags configured with
`-DREGENERATE_SOURCES=OFF` needs only the ARM toolchain, `make`, `cmake`, a
native compiler and the ChibiOS submodule -- no `fmpp`, no Java runtime, no
nanopb generator, no `protoc`, no Python. The default configure (`AUTO`) still
needs all of those, because prototype boards and proto variants generate their
sources (see [What a build actually needs](#what-a-build-actually-needs)).
Generated sources are committed for the distributed set, every image carries a manifest keyed
on its own SHA-256, and the conditions that would quietly make a build
unreproducible are detected at configure time. A commit determines its image
byte for byte. The gaps that remain are in the [build worklist](TODO.md).

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
[Field Data Extraction](../../embedded/tags/design/proposals/field-data-extraction.md); the two meet at the image
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
and `UIUCTag` -- five of the fourteen configured. `BitTag-legacy` is not
marked: it does not currently build, which is exactly the kind of thing the tier
is meant to keep out of a release.

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
committed, so they are also category 1 -- described by the commit, and not
rebuilt unless their recorded inputs change:

| Output | From | Committed for the distributed set? |
| --- | --- | --- |
| `board.{c,h,mk}`, `board_standby.h` | `fmpp` + ChibiOS `.ftl` templates, via `generate_board_chcfg.py` | yes, in `embedded/boards/<board>/generated/` |
| `tag.pb.{c,h}`, `tagdata.pb.{c,h}` | `nanopb_generator` | yes, in `embedded/proto-c/<variant>-proto-c/generated/` |
| `default_config.c` | `config-gen.py` from `default-config.json` | yes, beside the `.pb.*` |
| `tag.options`, `tagdata.options` | `CombineFiles.cmake` over the default and per-variant options | no -- pure CMake, no external tool, regenerated cheaply |
| `cfg/board.chcfg`, `cfg/board.fmpp` | `generate_board_chcfg.py`, `configure_file` | no -- intermediates, only produced when regenerating |

**3. Shared external code.** ChibiOS and nanopb. Both are compiled into the
image, and neither is described by the firmware sources.

- **ChibiOS** is a submodule. The superproject records a SHA, and configure
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
| Inputs hashed | both `.proto` files, both halves of each options file, `default-config.json`, `config-gen.py`, `CombineFiles.cmake` | `board-customizations.json`, `generate_board_chcfg.py`, `board.fmpp.in`, the ChibiOS pin XML, `board.{c,h,mk}.ftl`, and every file in ChibiOS `tools/ftl/libs/` |
| Tool line | `nanopb <version>` | `chibios-templates <processor>` |

Three choices in there are worth keeping in mind:

- **Timestamps are not used.** Git does not preserve mtimes, so a fresh clone
  would look stale and a checkout could look fresh when it is not.
- **ChibiOS inputs are recorded under a fixed `<chibios>` label**, not a relative
  path, because `CHIBIOS_DIR` may point outside the repository and `../../..` in
  a manifest would differ between machines while meaning the same thing. Their
  contents are still hashed, so a submodule bump still shows up.
- **`fmpp`'s own version is deliberately absent** from the board manifests. A
  manifest has to compare equal across machines, and a tool version reported on
  one machine and not another would cause false staleness. Instead configure
  checks the `fmpp` it finds against the `FMPP_VERSION` pin (0.9.16) whenever
  boards can regenerate. The per-image build manifest does not record `fmpp`
  either; that gap is in the [build worklist](TODO.md).

### The two switches

`REGENERATE_SOURCES` decides whether committed generated sources may be rebuilt:

| value | behaviour |
| --- | --- |
| `AUTO` (default) | regenerate exactly when the recorded inputs no longer match |
| `ON` | always regenerate, and write the result back into `generated/` |
| `OFF` | never regenerate; a stale committed source is reported, not fixed |

`REPRODUCIBLE_BUILD` decides how loudly anything unreproducible is reported.
`OFF` (the default) warns, so day-to-day work is not obstructed. `ON` turns those
warnings into configure errors (the two exceptions are marked in the table
below), which is how CI and release qualification build.

Under `OFF`, targets that commit nothing -- prototypes and bases -- are
configurable but unbuildable: asking for one fails with a message saying to use
`AUTO`. A release build configures the whole tree and builds only the
distributed targets, so they have to be configurable; failing at the moment one
is actually requested says the same thing where it matters.

### What is checked at configure time

Most of these go through `reproducibility_problem()` in
`cmake/ReproducibilityChecks.cmake`, so `REPRODUCIBLE_BUILD=ON` makes them
errors. The two marked *warning only* stay warnings even in strict mode:

| Condition | How it is detected |
| --- | --- |
| Working tree dirty | `git status --porcelain`, modified tracked files only |
| Vendored nanopb runtime edited | `git status --porcelain --untracked-files=normal`, scoped to `embedded/thirdparty`, so an added file counts too |
| Submodule uninitialized, off its recorded commit, or unmerged | `git submodule status` leading `-`, `+`, `U` |
| Submodule clean but dirty inside | `git status --porcelain` within it |
| Submodule not on the branch `.gitmodules` tracks | `git merge-base --is-ancestor HEAD origin/<branch>` |
| Toolchain other than `ARM_TOOLCHAIN_VERSION` | `arm-none-eabi-gcc -dumpversion` |
| `fmpp` other than `FMPP_VERSION` (0.9.16), when boards can regenerate | `fmpp --version`; skipped under `REGENERATE_SOURCES=OFF` |
| Committed generated sources stale | manifest comparison, per variant and per board |
| Generator version disagrees with the vendored runtime | `nanopb_generator --version` |
| `protoc` and the pinned Python protobuf (`embedded/proto-c/requirements.txt`) are different releases, when sources can regenerate | `protoc --version` against the installed package version |
| Distributed target that commits nothing | the derived sets, compared |
| Committed generated files that no distributed tag uses (*warning only*) | the derived sets, compared |
| HEAD not at a tag (*warning only*) | `git describe --exact-match`, strict mode only |

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
generator reported at configure time. Configure looks for a generator in every
mode -- under `NANOPB_ROOT` first, then on `PATH` -- and records its version
when it finds one, even under `REGENERATE_SOURCES=OFF`, where nothing runs it.
CI installs no generator, so a CI manifest leaves the field blank. Neither a
blank nor a filled field says that a generator produced the sources in that
build.

**In the image**, `version.h` defines `VERSION_HASH`, `GIT_SHA`, `GIT_DATE`,
`GIT_COMMIT_SUBJECT`, `GIT_REPO`, `GIT_DIRTY`, `GIT_DIRTY_STR`,
`CHIBIOS_SHA` and `NANOPB_RUNTIME_VERSION`. The last four are macros that cost
nothing unless referenced, and nothing references them yet -- see the
[build worklist](TODO.md).

### What a build actually needs

| Tool | Needed for a distributed-tag firmware build? |
| --- | --- |
| `arm-none-eabi-gcc` 14.2.1 and binutils | always; version is pinned and checked |
| `make`, `cmake` | always |
| `python3` | to regenerate a board, and to build the `config-gen` virtualenv |
| `fmpp` and a Java runtime | to regenerate a board |
| `nanopb_generator` | to regenerate protocol sources |
| `protobuf` 5.28.1 in a virtualenv, and a `protoc` | to regenerate protocol sources; the protobuf version is pinned in `requirements.txt`, and `protoc` is checked against it |

"To regenerate" includes the default configure. Under `AUTO`, the ten boards
configured from customizations without a `generated/` directory (four bases and
six prototype boards) and the four proto variants without one (`bittag-legacy`,
`bittag-ng`, `prestagraw`, `bitprestag`) always regenerate, so on a machine
without `fmpp` or a nanopb generator `AUTO` stops at configure with an error. It
also builds the `config-gen` virtualenv, which needs network access the first
time, and stops if no `protoc` is found -- it looks beside the nanopb generator,
then at `Protobuf_PROTOC_EXECUTABLE`, then on `PATH`.

With `REGENERATE_SOURCES=OFF` none of the optional rows are needed: no
virtualenv is created, no interpreter or `protoc` is searched for, the C++
protobuf library is not looked for either, and `fmpp` and `python3` are looked
up leniently. Configure still covers the whole tree, but only a target whose
board and proto variant both commit their generated files can be built without
the generators; the distributed tags qualify. Asking for a target whose board or
proto variant is generated fails with a message saying to use `AUTO`, except
for the three boards still on the older `generate_board_files()` path
(`bittag-base-jlcpcb-v3`, `tag-breakout-base-jlcpcb32-v1`, `TagSteval`), which
run `fmpp` at build time in every mode.

### CI

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `embedded-reproducibility.yml` | push to `main`, pull requests, and `workflow_dispatch` | runs `cmake/CheckGeneratedSourcesFresh.cmake` over the committed proto variants and boards |
| `release-firmware.yml` | `fw-vX.Y` tags, and `workflow_dispatch` | installs the pinned toolchain, configures with `-DREGENERATE_SOURCES=OFF -DREPRODUCIBLE_BUILD=ON`, builds `distributed_firmware`, publishes images with their manifests |
| `generated-sources-reproduce.yml` | weekly, and `workflow_dispatch` | installs the pinned generators, regenerates every committed source, and fails if the generated code differs from the tree ([decision 0014](../decisions/0014-build-generated-sources-reproduce-on-any-platform.md)) |
| `release.yml` | `vX.Y` tags | host tools only |

The freshness check reuses `InputManifest.cmake`, so it cannot disagree with what
the build writes, and it reads the `.proto` list out of `proto/CMakeLists.txt`
and each board's `PROCESSOR` out of its own `CMakeLists.txt` rather than
repeating either. It needs CMake and a checkout -- no ARM toolchain, no `fmpp`,
no generator. A check that needed the generator in order to prove the generator
is unnecessary would be self-defeating. ChibiOS is checked out because the board
templates are inputs, and `REQUIRE_CHIBIOS=ON` makes a missing submodule a
failure, since in CI a skipped check looks exactly like a passing one.

CI builds and publishes candidates; releasing them is in
[Releasing Tag Firmware and Host Tools](../release/release-procedure.md).

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
| Bump nanopb | replace the vendored directory, point `NANOPB_ROOT` at the matching generator, update the protobuf pin in `embedded/proto-c/requirements.txt` and `NANOPB_URL`/`NANOPB_SHA256` in `generated-sources-reproduce.yml`, regenerate, commit |
| Change the ARM toolchain | update `ARM_TOOLCHAIN_VERSION` in `CMakeLists.txt` and the URL and SHA-256 in `release-firmware.yml` and `generated-sources-reproduce.yml` |

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
4. Set `protobuf` in `embedded/proto-c/requirements.txt` to the release of the
   `protoc` that distribution ships (protoc 28.1 is protobuf 5.28.1). Configure
   compares the two and reports a mismatch, which strict mode makes an error.
5. Update `NANOPB_URL` and `NANOPB_SHA256` in
   `.github/workflows/generated-sources-reproduce.yml`.
6. Regenerate under `ON` and commit. The generated headers carry the generator
   version in a comment, so the diff shows the bump.

### Changing the ARM toolchain

`ARM_TOOLCHAIN_VERSION` in `CMakeLists.txt` is what every build checks against;
`release-firmware.yml` and `generated-sources-reproduce.yml` each pin the
tarball URL and its SHA-256 for CI. All three must move together, or CI installs
one version and the build rejects it.

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

Because `SHAStr` carries the commit into the image, builds of different
commits always have different hashes, even when nothing else changed. The
converse is the case that matters -- same commit, different bytes -- and the
manifest's hash is what distinguishes it.

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

## Decisions and open work

The choices behind this model, with the evidence for each:

- [0010: an image is determined by its commit](../decisions/0010-build-image-is-determined-by-the-commit.md) --
  no compile-time timestamps or clone-dependent strings in the image; verified
  byte-identical across machines.
- [0011: nanopb is pinned at 0.4.9.1](../decisions/0011-build-pin-nanopb-0-4-9-1.md).
- [0013: `config-gen` is Python, with protobuf pinned](../decisions/0013-build-config-gen-in-python-with-pinned-protobuf.md).
- [0014: generated sources reproduce on any platform](../decisions/0014-build-generated-sources-reproduce-on-any-platform.md),
  checked by `generated-sources-reproduce.yml`.
- [0015: `fmpp` and its rendering environment are pinned](../decisions/0015-build-pin-fmpp-and-its-rendering-environment.md).

How the build was found to be unreproducible, and the first release run, are in
the [build reproducibility investigation](../investigations/2026-09-firmware-build-reproducibility.md).
Known gaps and open questions are in the [build worklist](TODO.md).
