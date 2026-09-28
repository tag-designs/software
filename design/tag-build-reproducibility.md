# Tag Firmware Build Reproducibility

Status: partly implemented. The nanopb runtime is vendored, the build compiles
against it, the version agreement is checked at configure time, and the
line-ending rules are in place. Committing generated sources, the build
manifest, the remaining pitfall checks and the CI work are not started. See
[State of the work](#state-of-the-work) at the end for what is done and what is
next.

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

The distinction the tree already wanted is the one CMake supplies: **the
actively deployed tags are the ones that install.** `add_embedded_target` takes
a `DISTRIBUTE` keyword; marked targets install their firmware artifacts and are
recorded in the global property `ULTRALIGHT_DISTRIBUTED_TAGS`. That gives three
tiers:

| tier | how it is expressed | obligation |
| --- | --- | --- |
| retired | commented out of `embedded/tags/CMakeLists.txt` | none |
| prototype | added, but without `DISTRIBUTE` | must compile |
| distributed | `add_embedded_target(... DISTRIBUTE)` | reproducible: committed generated sources, pinned tool versions, version agreement enforced, firmware in the release |

The distributed set is currently `BitTag`, `CompassTagAT25`,
`IMUTagNandBmp581`, `PresTag` and `UIUCTag` -- five of the fourteen configured.
`BitTag-legacy` was marked and then unmarked: it does not currently build, which
is exactly the kind of thing the tier is meant to keep out of a release.

The proto-c variants needing the same treatment are **derived, not declared**.
`add_embedded_target` already knows each tag's proto target, so the distributed
proto set falls out of the tag markings and is recorded in
`ULTRALIGHT_DISTRIBUTED_PROTO_TARGETS`; there is no second list to fall out of
step. For the five tags above it resolves to `bittag_proto`, `compasstag_proto`,
`imutag_proto`, `prestag_proto` and `uiuctag_proto` -- five of the nine variants
configured.

Two aggregate targets fall out of the marking:

- `distributed_firmware` builds every tag that ships.
- `distributed_proto_sources` regenerates the proto-c outputs for exactly that
  set, which is what the regeneration and freshness steps below drive.

Neither existed before, and their absence was not merely inconvenient. The
per-tag targets are not in `ALL`, and `install(FILES)` of the firmware artifacts
creates no build dependency -- which is why those install rules carry
`OPTIONAL`. So `make install` on a fresh tree installed nothing at all, silently.
`make distributed_firmware && make install` is the sequence that produces a
populated package, and CI will want the same pair.

Everything that follows applies to the distributed set. Prototypes keep
generating their sources at build time and are not held to the freshness check;
`REPRODUCIBLE_BUILD=ON` escalates to an error only for tags that install. A
prototype that later goes into the field is promoted by adding one keyword,
which is the point of choosing a marker the build already acts on rather than a
list in a document.

## What a build consumes

### Three categories of source

**1. Present in the repository.** Tag, board and common firmware sources,
`project.mk`, the `.proto` definitions, per-board customizations, per-variant
`*.override.options`. Fully described by the commit.

**2. Generated from files in the repository.** Not versioned today -- produced
into the build tree at configure or build time:

| Output | From |
| --- | --- |
| `cfg/board.chcfg`, `cfg/board.fmpp` | `generate_board_chcfg.py` + ChibiOS `tools/ftl/xml/<proc>board.xml` |
| `board.{c,h,mk}`, `board_standby.h` | `fmpp` + ChibiOS `.ftl` templates |
| `tag.options`, `tagdata.options` | per-variant `*.override.options` |
| `tag.pb.{c,h}`, `tagdata.pb.{c,h}` | `nanopb_generator` |
| `default_config.c` | `config-gen` from `default-config.json` |

**3. Shared external code.** ChibiOS and nanopb. Both are compiled into the
image, and neither is described by the firmware sources.

- **ChibiOS** is a submodule, so the superproject records a SHA. That pins it
  *if* the submodule is actually at the recorded commit and clean, which nothing
  currently checks. `embedded/CMakeLists.txt` does warn when `CHIBIOS_DIR` comes
  from the environment instead of the submodule, which is the right instinct and
  the only such check in the tree.
- **nanopb plays two roles**, and until recently both came from one untracked
  tree: `NANOPB_SRC_ROOT_FOLDER` supplied the include path for the *runtime*
  -- `pb_encode.c`, `pb_decode.c`, `pb_common.c`, `pb.h`, compiled into every
  shipped image -- and the `generator-bin` hint for the *generator* that
  produces the `.pb.*` sources. The tree was listed in `.gitignore` and supplied
  per developer, so neither role had a recorded version.

  **What that produced, found while fixing it:** the tree in use was a git clone
  at `nanopb-0.4.8-11-g1f0c2e1`, an untagged master snapshot whose `pb.h`
  self-reports `0.4.9-dev`, while the generator beside it reported `0.4.9.1`.
  Runtime and generated code disagreed in every image built on that machine.
  `PB_PROTO_HEADER_VERSION` is 40 for both, so the one safety net nanopb
  provides could not see it -- it catches 0.3 against 0.4, not 0.4.7 against
  0.4.9.1.

  **Now:** the runtime is vendored at `embedded/thirdparty/nanopb-0.4.9.1/` and
  the build compiles against it; `NANOPB_SRC_ROOT_FOLDER` supplies only the
  generator; and configure compares the directory-derived version, the vendored
  `pb.h` and the generator's reported version. The two roles are treated
  separately under strategy 2 below, which describes the arrangement as built.

### Tools

| Tool | Produces | Needed to build? | Pinned? |
| --- | --- | --- | --- |
| `arm-none-eabi-gcc` / binutils | the image | always | no |
| `make`, `cmake` | the build | always | no |
| `fmpp` (Java) | board files | only when regenerating | no |
| `python3` + `generate_board_chcfg.py` | `board.chcfg` | only when regenerating | script in repo |
| `nanopb_generator` | `.pb.{c,h}` | only when regenerating | not today; by version under strategy 2, plus an archive hash for CI |
| `config-gen` + host protobuf | `default_config.c` | only when regenerating | source in repo, links protobuf |

"Only when regenerating" describes the intended state, not the current one.
Today all of these are required for an ordinary firmware build: a JVM, an
untracked Python generator, and a full host C++ protobuf build stand between a
clean checkout and the first ARM object.

## Strategy

Three lines of attack, in order of how much they buy:

1. **Minimize dependence on external tools** by committing generated code.
2. **Record library and tool versions** for what remains.
3. **Flag inputs that make a build unreproducible**, loudly and early.

### 1. Commit generated sources

Moving category 2 into category 1 removes `fmpp`, `nanopb_generator` and
`config-gen` from the build dependency set, leaving them needed only when
regenerating. A firmware build then needs the compiler, `make` and the ChibiOS
submodule.

**What to commit.** Each generator was checked for what it stamps into its
output, because a freshness check is only possible against a deterministic
generator:

| Output | Stamp | Commit |
| --- | --- | --- |
| board files | none -- the ChibiOS `.ftl` templates emit no date or version | yes |
| nanopb `.pb.{c,h}` | `/* Generated by nanopb-0.4.9.1 */`, version only | yes |
| `default_config.c` | none observed | yes |
| host `*.pb.{cc,h}` | `// NO CHECKED-IN PROTOBUF GENCODE` | **no** |

The host C++ gencode carries protobuf's own instruction not to check it in, and
enforces a runtime-version guard that would couple the tree to whatever version
vcpkg resolves. It is also not part of a tag image. Leave it generated.

nanopb's version banner works in favour of committing: a committed `.pb.h`
states which generator produced it, so the tree records the version without a
separate note, and a regeneration by a mismatched generator opens its diff with
a one-line version change that accounts for every other hunk.

**Why this matters beyond convenience.** `board.c` is rendered from ChibiOS's
templates, so a submodule bump can change the GPIO initialisation compiled into
every image. Given that Standby entry is layout-sensitive, a silent relayout is
the class of change this project cannot afford to find on a Joulescope weeks
later. Committed, it arrives as a reviewable diff.

**Mechanics.**

- In-source `generated/` directories beside the inputs that produce them:
  `embedded/boards/<Board>/generated/`,
  `embedded/proto-c/<variant>/generated/`. Each file keeps its generator's
  "do not edit" banner.
- **Generation stays automatic where the tools exist.** Board customizations
  and `.proto`/`.options` files change often during tag development, and a
  manual step that can be forgotten is worse than no step. The existing
  `add_custom_command` rules already declare complete dependencies -- the
  customizations JSON, the ChibiOS XML, all three `.ftl` templates,
  `generate_board_chcfg.py`, `${TAG_PROTO_SOURCES}`, the merged options,
  `default-config.json` -- so redirecting their `OUTPUT` from the build tree to
  the source tree makes regeneration incremental and automatic. Edit an input,
  build, and the generated file updates and appears in `git status`.
- **`REGENERATE_SOURCES` is a tri-state**, not a switch:
  - `AUTO` (default): wire up the generation rules when the generators are
    found; fall back to the committed copies when they are not.
  - `ON`: require the generators and fail at configure time if any is missing.
    Used by the CI freshness job.
  - `OFF`: never generate. Used by the CI firmware build, so the no-tools path
    is proven rather than assumed.

  `find_program(FMPP ... REQUIRED)` and the nanopb generator lookup must become
  conditional rather than unconditional as they are now, and `config-gen` must
  build only when regenerating.
- **An input digest covers the `AUTO`-without-tools case**, which is the one
  CMake cannot catch by dependency: someone edits a board customization on a
  machine with no `fmpp`, no rule exists, and the build silently uses stale
  output. Alongside each generated directory write `.inputs.sha256`, a digest
  over the inputs that produced it. Recomputing it needs only file hashing, no
  generator, so configure can always compare and name the input that moved.
- Keep the explicit `regenerate-boards`, `regenerate-proto-c` and aggregate
  `regenerate` targets for the `OFF` case and for forcing output after a
  generator upgrade, where no input changed but the output would.
- `.gitattributes`: mark the nanopb outputs `linguist-generated`, and set
  `eol=lf` on every generated path. Generated text written on Windows otherwise
  lands with CRLF and produces diffs with nothing to do with content. Set this
  before the first commit of generated output, not after.

**Freshness is enforced in CI**, by a job that configures with
`-DREGENERATE_SOURCES=ON`, runs `regenerate`, and fails on `git diff
--exit-code` over the generated paths. A diff means "run the regenerate target
and commit the result", not "CI is broken", and the job should say so.

The two checks are complementary and catch different failures. The digest
catches an input edited without regenerating -- the frequent development
mistake. The CI diff catches what the digest structurally cannot: a **generator
upgrade** changes the output while every input hash stays identical.

**One platform is authoritative.** The CI job runs on Linux with the pinned
generator distribution, and its verdict is the one that counts. A local version
mismatch warns rather than fails, so that whoever is working on Windows or macOS
with a different point release does not get red builds for a difference the
pinned environment does not have.

### 2. Record library and tool versions

What cannot be eliminated must be recorded. Two places, for two audiences.

**Pin the libraries, by role.** ChibiOS is one thing -- a submodule whose SHA
the superproject records. nanopb is two, and they want different mechanisms.

*The nanopb runtime is source that ships inside the product.* `pb_encode.c`,
`pb_decode.c`, `pb_common.c` and `pb.h` are compiled into every image. They are
platform-independent and there are about seven of them, so **vendor them into
the tree** -- `embedded/thirdparty/nanopb-0.4.9.1/`, the exact version in the
directory name, with a README recording the upstream commit and the archive it
came from. Vendoring rather than a submodule keeps them in this repository's
history permanently, instead of depending on an upstream archive still being
downloadable years from now, which is the whole premise of archiving over
rebuilding.

Naming the directory for the version rather than keeping a fixed `nanopb/` path
has three consequences, two of them wanted:

- The version is legible from a directory listing and from the include path, and
  becomes another statement that must agree with the pin -- see below.
- An upgrade becomes an explicit new directory rather than an in-place
  overwrite, so it cannot happen by accident, and a stale build directory
  pointing at the old path fails loudly instead of quietly compiling the old
  runtime.
- Against that: git records the upgrade as a delete and an add rather than a
  modification, so reviewing *what changed inside* `pb_encode.c` between
  versions relies on rename detection. `git diff -M` and the GitHub UI normally
  pair the files, but it is worth using `--find-renames` deliberately when
  reviewing a nanopb bump.

Only the CMake include-directory line references the path; sources use
`#include <pb.h>`, so the rename costs one or two lines.

Concretely that is `pb.h`, `pb_common.{c,h}`, `pb_encode.{c,h}`,
`pb_decode.{c,h}` and the licence -- about eight files -- taken from the
source archive, with a README beside them recording the version, the upstream
commit, the archive it came from and that archive's hash.

A plain vendored copy rather than a submodule, for three reasons: the whole
nanopb repository is far more than these files; a copy needs no network at
build time and survives upstream going away, which is the premise of archiving
over rebuilding; and the copy is diffable, so an upgrade is reviewable rather
than a SHA change. `git subtree` sits in between and preserves upstream
history, at a complexity cost this does not need.

*The nanopb generator is a build-time tool*, like `arm-none-eabi-gcc`. It does
not belong in the repository; it needs to be pinned and verified.

The source archive and the binary distribution agree on the runtime: the eight
runtime files in `nanopb-0.4.9.1-macosx-x86` are byte-identical to the upstream
git tag, so vendoring from source while pinning the distribution introduces no
divergence.

Here the precompiled nanopb distributions are an asset rather than a
compromise. Each bundles the generator, a `protoc`, and a compatible Python
protobuf package as one unit, so pinning the distribution pins all three
together. Wiring nanopb in as a `protoc` plugin from source would replace one
pinned thing with three independently versioned ones that must stay mutually
compatible.

`tools/make_mac_package.sh` shows where that unit comes from: PyInstaller
bundles `protoc` out of the `grpc_tools` package in the build environment, so
the bundled protoc version is really the `grpcio-tools` version pinned at
package-build time. The manifest should therefore read it from the bundled
binary -- `generator-bin/protoc --version` -- rather than from any system
protoc. The same script also emits `protoc-gen-nanopb`, so the plugin path is
shipped ready-made in the binary package.

**Pin the version everywhere; pin the bytes only where it is authoritative.**

- `NANOPB_VERSION` is recorded in one file in the repository, and configure
  compares the found generator's reported version against it -- warning locally,
  failing under `-DREPRODUCIBLE_BUILD=ON`. A locally built arm64 generator at
  the pinned version satisfies this exactly as well as the upstream x86 one.
- The **SHA-256 of the Linux archive** is recorded as well, because the CI
  freshness job is the verdict that counts and is the one place where fetching
  exact bytes costs nothing. A script fetches and verifies it into a
  build-local cache.

Recording a hash per developer platform was the earlier proposal and it buys
little: a Mac hash constrains a machine whose regeneration is advisory anyway,
while obliging someone who builds their own arm64 generator to update a hash
that describes nobody else's build.

Upstream publishes stable releases at <https://jpa.kapsi.fi/nanopb/download/>
with a stable naming pattern, which is what makes such a script writable:

| Archive | Contents |
| --- | --- |
| `nanopb-<version>.tar.gz` | source only -- the vendored runtime comes from here |
| `nanopb-<version>-linux-x86.tar.gz` | generator binaries |
| `nanopb-<version>-macosx-x86.tar.gz` | generator binaries |
| `nanopb-<version>-windows-x86.zip` | generator binaries |

Four things follow from that index:

- **Availability does not constrain the pin.** All three platform archives exist
  for every release from 0.4.7 through 0.4.9.2, so any of them can be pinned.
- **0.4.9.x is the upstream LTS line**, with bugfixes backported as `x.x.9.x`
  releases. That is the line to pin to, and the tree's committed output is
  currently generated by 0.4.9.1 (2024-12-01) while 0.4.9.2 (2026-08-24) exists.
- **The macOS archive's name does not describe its architecture.** The 0.4.9.1
  `macosx-x86` archive contains `Mach-O 64-bit arm64` binaries for both
  `nanopb_generator` and `protoc` -- measured with `file`, not inferred -- so no
  Rosetta is involved on Apple Silicon. `tools/make_mac_package.sh` only puts
  `-macosx-x86` in the archive *name*:

  ```sh
  VERSION=`git describe --always`-macosx-x86
  ```

  No script forces an architecture; PyInstaller builds for whatever host it runs
  on, and upstream evidently built 0.4.9.1 on Apple Silicon. Two consequences:
  the architecture of a published macOS archive can change from release to
  release without the name changing, so it should be checked with `file` rather
  than assumed either way; and an Intel Mac cannot run this particular "x86"
  archive at all. Building the package locally is the fallback in either
  direction -- run the script in a git clone at the tag rather than from the
  source tarball, since the version comes from `git describe`.
- **Checksums are published on the project's forum**, not beside the archives.
  So the Linux archive hash recorded in this repository is computed once when
  the version is pinned and verified against from then on. That is trust on
  first use, which is what pinning means, but it should be understood as such
  rather than mistaken for verification against an upstream manifest.

**If Rosetta goes away**, nothing urgent breaks, because committing generated
sources means a generator is needed only to *change* a `.proto` or `.options`
file. Distribution needs none at all. Three fallbacks, in increasing order of
effort: a developer on Apple Silicon builds the generator from a nanopb clone at
the pinned tag, as above; or regenerates in a Linux container; or does not
regenerate, which costs nothing unless they are the one changing the protocol.
Regeneration on Linux alone is sufficient for everything except day-to-day
convenience, so the contingency is a convenience loss, not a capability loss.

For scale of the hole being closed: the generator is currently found at a path
like `/Users/geobrown/Software/nanopb`, outside the repository, recorded only in
a build cache.

The vendored runtime and the pinned generator must name the same nanopb
version, and the `PB_PROTO_HEADER_VERSION` guard in generated headers catches
only a major mismatch, so the configure-time comparison is what covers point
releases.

**Upgrading nanopb is one commit.** The runtime and the generated code must
move together -- a runtime from one version with `.pb.c` from another is a
defect, caught only coarsely by `PB_PROTO_HEADER_VERSION`. Vendoring is what
makes moving them together possible atomically:

1. Bump `NANOPB_VERSION` and the recorded archive hash.
2. Replace the vendored runtime files from the new source archive.
3. Regenerate every `.pb.*` with the matching generator.
4. Commit the three together; CI's freshness check then confirms the committed
   output is what the new pinned generator produces.

The version banner in each generated header makes step 3 visible in the diff,
and the whole change is reviewable as a unit. This is worth contrasting with
today: with nanopb supplied per developer, there is no way to make that change
atomically at all -- the runtime moves when each person happens to update a
directory outside the repository, and the generated code moves whenever someone
next regenerates.

### Enforcing the version agreement automatically

A bump to `NANOPB_VERSION` that is not accompanied by the other changes is the
failure this arrangement is most exposed to, and it can be caught at configure
time without git history and without the generator, because the tree states its
nanopb version in five independent places:

| Statement | Where | Needs |
| --- | --- | --- |
| the pin | `NANOPB_VERSION` in the version file | nothing |
| the vendored directory | `embedded/thirdparty/nanopb-0.4.9.1/` | nothing |
| the vendored runtime | `#define NANOPB_VERSION "nanopb-0.4.9.1"` in `pb.h` | nothing |
| the generated code | `/* Generated by nanopb-0.4.9.1 */` in every `.pb.h` | nothing |
| the tool | the generator's reported version | the generator |

Three of the four are file reads, so the check runs on a machine with no
generator at all -- the `AUTO`-without-tools case -- and compares what is
actually in the tree rather than what a commit claims:

The directory name is the cheapest of the five, and can be made a derivation
rather than a comparison: glob `embedded/thirdparty/nanopb-*`, require exactly
one match, and take the version from it. A second directory left behind by a
half-finished upgrade then fails immediately rather than leaving the build to
pick one.

```cmake
file(GLOB _nanopb_dirs "${CMAKE_SOURCE_DIR}/embedded/thirdparty/nanopb-*")
list(LENGTH _nanopb_dirs _n)
if(NOT _n EQUAL 1)
  message(FATAL_ERROR "expected exactly one vendored nanopb, found ${_n}")
endif()
list(GET _nanopb_dirs 0 NANOPB_VENDOR_DIR)

file(READ "${NANOPB_VENDOR_DIR}/pb.h" _pb_h)
string(REGEX MATCH "NANOPB_VERSION[ \t]+\"nanopb-([0-9.]+)\"" _m "${_pb_h}")
set(_runtime_version "${CMAKE_MATCH_1}")

file(STRINGS "${_generated_dir}/tagdata.pb.h" _banner REGEX "Generated by nanopb-")
string(REGEX MATCH "nanopb-([0-9.]+)" _m2 "${_banner}")
set(_generated_version "${CMAKE_MATCH_1}")
```

Compare both against `NANOPB_VERSION`, and the generator's reported version too
when one is found. Warn on any disagreement; fail under
`-DREPRODUCIBLE_BUILD=ON`. Quote the match variables in the comparison --
`if("${CMAKE_MATCH_1}" STREQUAL ...)` -- because an unmatched group leaves
`CMAKE_MATCH_n` undefined rather than empty, and an unquoted `if` then compares
the literal variable name.

Each disagreement has a distinct meaning, and the message should say which:

| Disagreement | What happened |
| --- | --- |
| pin ahead of directory | version bumped, directory not renamed |
| pin ahead of runtime | version bumped, vendored files not replaced |
| pin ahead of generated | version bumped, `.pb.*` not regenerated |
| runtime ahead of generated | runtime replaced, `.pb.*` not regenerated |
| generator differs from pin | local tool is not the pinned one |

**Implemented, in part.** The pin, the vendored `pb.h` and the generator's
reported version are compared at configure time, with `nanopb_version_problem()`
warning by default and failing under `-DREPRODUCIBLE_BUILD=ON`. The generated
banner is not compared yet: the `.pb.*` files are regenerated on every build by
the generator that was just checked, so until they are committed the generator
check subsumes it. That comparison becomes necessary at the same moment the
generated sources enter the repository.

A generator that cannot report its version warns but never fails, even under
`REPRODUCIBLE_BUILD`. That is a gap in the evidence rather than proof of a
mismatch, and a build should not be blocked over a tool's command line when the
tree itself is consistent.

Determinism of the banner is not an assumption: `nanopb_generator` defaults
`notimestamp` to true and has since 0.4.0, so the preamble carries the version
alone. Passing `-t` would add `time.asctime()` and make every regeneration
differ, which is worth knowing before anyone adds generator flags.

A CI job could instead check that a diff touching the version file also touches
the vendored and generated paths. That is weaker: it depends on how the change
was made rather than on what the tree contains, and it would miss a bad merge or
a direct edit. The content check subsumes it.

The same shape applies to ChibiOS -- the recorded submodule SHA against the one
checked out -- so this is one routine with several instances rather than a
special case for nanopb.

### Two protobuf toolchains, pinned separately

"The protobuf tool" is two different things here, and keeping them distinct
avoids a false coupling:

| | Pinned by | Produces |
| --- | --- | --- |
| Host protobuf / `protoc` | the vcpkg baseline | host `*.pb.{cc,h}`, generated at build time |
| Embedded `protoc` | `NANOPB_VERSION`, transitively -- it is bundled from `grpc_tools` when the nanopb package is built | nothing directly; it parses `.proto` for the nanopb generator |

The isolation is useful: a vcpkg baseline bump changes host gencode and leaves
the committed `.pb.*` untouched, and a nanopb bump does the reverse.

**One coupling crosses that line, and it is weaker than it looks.**
`config-gen` links the host protobuf library, so `default_config.c` -- which is
compiled into the image -- is produced by a host-side tool. But what it produces
is not implementation-dependent:

```cpp
JsonStringToMessage(str, &configin, options2);
...
configin.SerializeToString(&output);
```

It parses the JSON into a `Config` and emits the **proto3 wire encoding** as a
byte array. The wire format is specified and stable across implementations and
versions -- that is its purpose -- so the real inputs are `tag.proto` and
`default-config.json`, both in the repository, not Google's implementation of
the day. `Config` contains no `map` fields, so the one documented source of
serialization nondeterminism does not apply either.

The residue is small enough to record rather than guard: `SerializeToString`
does not contract byte-for-byte determinism even though the C++ implementation
emits in field-number order, and `JsonParseOptions` defaults could in principle
shift. Both would be caught by the CI freshness check. So the host protobuf
version belongs in the build manifest, where it costs nothing, but not in the
pitfall checks, where it would flag a rare and probably benign event.

Committing `default_config.c` is still clearly right -- it removes `config-gen`,
and with it the host protobuf build, from an ordinary firmware build.

**A small defect noticed while reading this.** `config-gen` prints the parse
error and then `return 0` on the failure path, so invalid
`default-config.json` exits successfully without writing the output file. The
build does fail, but as a missing-output error from the build system rather than
as the JSON error that caused it.

**Record the environment per build.** A `build-manifest.json` written beside
the image, carrying what the sources do not:

| Field | Why |
| --- | --- |
| `image_sha256`, `elf_sha256` | identity -- see below |
| `git_sha`, `git_describe`, `tree_dirty` | which commit, and whether it was clean |
| `chibios_sha`, `chibios_dirty`, `chibios_from_env` | the ChibiOS actually used, not the one recorded |
| `nanopb_runtime_version` | the vendored runtime actually compiled in |
| `nanopb_generator_version`, `protoc_version`, `generator_platform` | which distribution produced the committed `.pb.*` |
| `generated_state` | fresh / stale / unverified |
| `toolchain` | `arm-none-eabi-gcc --version`, binutils, `cmake --version` |
| `target`, `board_type` | which image this is |
| `compile_flags_digest` plus the full command lines | the `-D` problem, recorded rather than inferred |
| `host_protobuf_version` | produced `default_config.c`; recorded, low risk |
| `project_mk_sha256` | the per-target options file |

The flags entry is the one that earns its place. It is the documented way a test
image and a shipping image differ while sharing a git hash, and it is invisible
in every other record.

**Record a summary in the image.** A manifest can be lost; the image is what
comes back from the field. `cmake/version.cmake` already generates `version.h`
with `GIT_SHA`, `VERSION_HASH`, date, subject and repo URL. Extending it with a
dirty flag and the submodule SHAs costs a few bytes of flash and makes a tag
able to state its own provenance over the monitor. `version.h` already changes
on every commit, so this adds no new rebuild churn.

### 3. Flag the pitfalls

Detection is worth little if it is a line in a log nobody reads. Each condition
below should be detected at configure time, reported in the manifest, and
escalated according to the build mode.

| Condition | How it is detected | Why it breaks reproducibility |
| --- | --- | --- |
| Working tree dirty | `git status --porcelain` | the commit does not describe the sources |
| Submodule at a different commit than recorded | `git submodule status` leading `+` | the library is not the one the commit names |
| Submodule working tree dirty | `git status --porcelain` inside it | edits invisible to the superproject |
| `CHIBIOS_DIR` outside the repo | already warned for | the library is not described at all |
| Vendored nanopb runtime edited in place | `git status --porcelain` on `embedded/thirdparty/nanopb-*` | a local patch compiled into shipped images |
| Generator version differs from the pin | `--version` against `NANOPB_VERSION` | different generator, different `.pb.*` |
| Generated files stale | regenerate and diff | committed sources are not what the inputs produce |
| Generated files unverified | generators unavailable | staleness unknown, not disproven |
| Toolchain differs from the recorded expectation | `--version` against a pinned string | a different compiler is a different image |
| Building at an untagged commit | `git describe --exact-match` | not a release candidate |

**Two modes, one switch.** `-DREPRODUCIBLE_BUILD=ON` turns every warning above
into a configure-time error; `OFF` is the default and warns. Day-to-day work
stays unobstructed, while CI and `tag_release_check.py` build strict, so
anything that could be flashed onto a tag that flies has been through the strict
path. The mode itself belongs in the manifest and in the image: a build that
was never strict should say so.

**A dirty build should be visible from the tag.** Reporting a dirty tree only at
build time loses the warning at the moment it matters -- months later, with the
tag on the bench and the manifest gone. One flag in `version.h` carried into the
image means a returned tag can say "the tree I was built from had uncommitted
changes", which is exactly the fact that would otherwise be silently assumed
away.

## Identity: the image hash

`VERSION_HASH` and `SHAStr` name a commit. They do not identify an image: a `-D`
leaves no trace in the git hash, and `git rev-parse HEAD` ignores uncommitted
changes. Two materially different binaries can report the same SHA.

**Key the archive on the SHA-256 of the image.** A capture that includes the
internal flash region contains the bytes, so hashing them identifies the build
with no metadata to trust and no firmware change required. The embedded git
identity stays useful as a human-readable label and for live readback over the
monitor, but it is not the key.

## The archive

Per image: the ELF with DWARF, the `.map`, the `.bin`, `project.mk`, the linker
script, `compile_commands.json`, the `build-manifest.json`, and -- where one
exists -- the external loader used to read that board's flash. Single-digit
megabytes.

Archive artifacts rather than planning to rebuild them. In three years the
toolchain will not install cleanly; the recorded versions make a rebuild
*possible*, but nothing downstream should depend on it. Reproducibility is the
cheaper of two insurance policies, not a substitute for the other.

## Building in GitHub Actions

Building firmware in CI serves this directly: it is the only way every image
that could reach a tag is produced by a known machine from a known commit, with
its manifest and artifacts archived automatically rather than by hand, and with
the strict mode above always on.

Committing generated sources should come first, because it reduces a CI firmware
build to the compiler, `make` and the submodules -- no JVM, no untracked
generator, no host protobuf build.

**CI cannot produce a release.** `tag_release_check.py` exists because the same
source can sleep or stall depending on where code lands, and a build that passes
every functional test can still draw 240x the idle current. That is measured on
hardware with a Joulescope. **CI produces candidates; only a bench measurement
produces a release**, and the qualification result should be recorded against
the image hash so the archive says not only what was built but whether it was
ever cleared to fly.

Firmware and host tools should use separate tag namespaces -- `fw-v*` against
the host tools' `v*` -- since they release on different clocks.

## State of the work

### Done

1. **The nanopb runtime is vendored** at `embedded/thirdparty/nanopb-0.4.9.1/`
   -- eight files, `CHECKSUMS.txt`, and a README recording the upstream tag and
   commit. Taken from the upstream tag and independently confirmed
   byte-identical to the `nanopb-0.4.9.1-macosx-x86` release distribution.
2. **The build compiles against it.** `NANOPB_RUNTIME_DIR` is derived by glob
   from `embedded/thirdparty/nanopb-*` and required to be unique; the proto
   targets' include directory points at it, which is what becomes `NANOPBDIR`
   and so supplies both `UINCDIR` for the headers and `VPATH` for
   `pb_common.c`, `pb_encode.c` and `pb_decode.c`. Verified on a real firmware
   build: every dependency file names the vendored path and none names the old
   tree. `NANOPB_SRC_ROOT_FOLDER` now supplies only the generator.
3. **The version agreement is checked** at configure time across the
   directory-derived pin, the vendored `pb.h` and the generator's reported
   version, with `REPRODUCIBLE_BUILD` turning warnings into errors. The
   generator lookup searches `generator-bin` and `generator` under
   `NANOPB_SRC_ROOT_FOLDER` before falling back to `PATH`, and says so when it
   falls back.
4. **`.gitattributes` is in place** -- `eol=lf` on `embedded/thirdparty/**` and
   on the paths generated sources will occupy, before any generated file is
   committed, because attributes do not normalize retroactively.
5. **The distributed set is marked.** `add_embedded_target` takes `DISTRIBUTE`,
   only marked targets install, and the distributed proto targets are derived
   from them. This scopes every remaining step to the shipped tags
   and their proto variants rather than all fourteen and nine. The `distributed_firmware` and
   `distributed_proto_sources` targets build and regenerate that set; the first
   also closes a standing gap, since `make install` had no way to build the
   firmware it was installing.

6. **Generated sources are committed** for the distributed variants --
   `tag.pb.c`, `tag.pb.h`, `tagdata.pb.c`, `tagdata.pb.h` and
   `default_config.c` under each variant's `generated/` directory, checked
   byte-for-byte against the same files generated independently on another
   platform. A variant declares that it commits by having that directory;
   prototypes still generate into the build tree, and
   `embedded/CMakeLists.txt` compares the committing set against the
   distributed one so the two cannot drift.
7. **`REGENERATE_SOURCES` is in place** as AUTO (the default), ON and OFF,
   driven by `generated/inputs.sha256`: the SHA-256 of every input the
   generators consume plus the pinned nanopb version. Not timestamps -- git
   does not preserve mtimes, so a fresh clone would look stale. The generator
   is no longer needed to configure, and under OFF `config-gen` is not built at
   all, which is what removes the host protobuf build from a firmware build.

Moving to 0.4.9.1 from the `0.4.8-11-g1f0c2e1` snapshot changes no behaviour
here: `pb_common.o` and `pb_encode.o` are byte-identical when cross-compiled
for `cortex-m4` and `cortex-m33`, and the sole semantic difference -- a leak fix
in `pb_decode_ex` under `PB_ENABLE_MALLOC` -- is in a function garbage-collected
out of the linked images, which call plain `pb_decode`.

8. **The pitfall checks are in place** in `cmake/ReproducibilityChecks.cmake`:
   dirty working tree, edited vendored runtime, submodule uninitialized or off
   its recorded commit or dirty inside it, toolchain other than the pin, and --
   in strict mode only -- a HEAD that is not at a tag. All go through
   `reproducibility_problem()`. `ARM_TOOLCHAIN_VERSION` is empty by default:
   recording the compiler version is unambiguously right, refusing to build on
   a different one is a policy to choose, so the comparison is opt-in.
   Untracked files are not treated as dirty -- nothing compiles them, and
   warning about scratch files would make the warning ignorable.

9. **The build manifest is written** as `<name>-build-manifest.json` beside each
   image, in the same target that links it: commit, describe, dirty flag, the
   ChibiOS commit actually compiled, tool paths and versions, the build modes,
   and the SHA-256 and size of the `.elf`, `.bin` and `.hex`. Git facts are read
   when the manifest is written, not at configure time, so it describes the
   image that exists. `version.h` gains `GIT_DIRTY`, `GIT_DIRTY_STR`,
   `CHIBIOS_SHA` and `NANOPB_RUNTIME_VERSION` as macros -- free unless
   referenced, and nothing references them yet, because adding a string to the
   image shifts its layout and STM32U375 Standby entry depends on where code
   lands. Carrying the dirty flag *into* the image is therefore a deliberate
   firmware change still to be made; the manifest holds the same facts
   meanwhile.

10. **The CI freshness check is in place.** The workflow
    `embedded-reproducibility.yml` runs `cmake/CheckGeneratedSourcesFresh.cmake` on
    every push and pull request. It reuses `InputManifest.cmake` so it cannot
    disagree with what the build writes, reads the `.proto` list out of
    `proto/CMakeLists.txt` rather than repeating it, and needs nothing but
    CMake -- no toolchain, no fmpp, no generator, no submodules.

### Next


11. **Build firmware in CI** for the distributed tags on a release tag, and
    publish the images with their manifests. This needs `arm-none-eabi-gcc`
    14.2.1 and `fmpp` on the runner, which is new CI surface; the freshness
    check above deliberately avoids both so that it cannot fail for their
    reasons.
12. **Carry the dirty flag into the image.** `version.h` defines it; nothing
    references it, because adding a string shifts the layout and STM32U375
    Standby entry depends on where code lands. A firmware change of its own.
13. **Check determinism directly** by building the same commit twice and
    comparing images. Commit-to-commit comparison cannot show this: `SHAStr`
    carries the commit into every image, so any two commits differ for a
    trivial reason.

### Where to resume

Step 11 is next and is the one with real unknowns left -- not about
reproducibility, which is settled, but about provisioning: whether the pinned
toolchain and `fmpp` install cleanly on a GitHub runner, and how long the
embedded build takes there.

Steps 12 and 13 are independent of it and of each other.

## Settled: pinning does not shift the generated output

The worry was that fixing on 0.4.9.1 would shift the `.pb.*` files away from
what built the firmware now in the field, making the first commit of generated
sources a silent behavioural change.

It does not. Both generators were run over six proto variants (the five
distributed ones and `bittag-legacy`, distributed at the time of the test) from the
repository's own `.proto` files and `CombineFiles.cmake`-combined options, and
all 24 outputs compared:

- **0.4.8 vs 0.4.9.1** -- identical but for the `/* Generated by nanopb-... */`
  comment and some trailing blank lines. No declaration, field descriptor,
  size macro or `PB_BIND` entry differs in any of the 24 files compared.
- **0.4.8 vs the tree's actual old generator** (`1f0c2e1`, the
  `0.4.8-11-g1f0c2e1` snapshot) -- the only code change to
  `nanopb_generator.py` across those eleven commits is inside
  `fields_declaration_cpp_lookup`, emitted solely under `--cpp-descriptors`,
  which this project does not pass. The rest is the version string and a
  comment.

So the chain from the field firmware's generator to the pinned one introduces no
substantive change, and the first commit of generated sources can be taken as a
baseline rather than staged behind a separate behaviour-changing commit.

### Settled: generator output does not depend on the platform

For nanopb 0.4.9.1, the macOS x86 release binary and the Linux PyPI wheel
produce **byte-identical** output: all 24 files across the six variants tested
compared equal, `.pb.c` and `.pb.h` alike, line endings included. The
two runs also used different include-path spellings, so the output is not
sensitive to that either.

A regeneration on a Mac can therefore be committed directly; there is no need to
make Linux authoritative or to route protocol changes through a particular
machine. This is what makes a CI freshness check meaningful -- had it not held,
the check would have failed for everyone not building on the blessed platform.

## Open questions

- **Pin 0.4.9.1 or move to 0.4.9.2?** Pinning what already generated the tree's
  output keeps the first commit of generated files a no-op; moving to the newer
  LTS bugfix release is a change worth making deliberately and separately.
- **How strictly should the toolchain be pinned?** Recording the version is
  clearly right; refusing to build on a different one may be more than a small
  group wants day to day, which is what the two modes are for -- but the
  expected version still has to be written down somewhere.
- **Where do manifests live** between the build and the release archive, for
  images built on a bench rather than in CI?
- **Do host tools need any of this?** Argued out of scope above, but the
  `dataprocessing` path also produces records that outlive their build.
