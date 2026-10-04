---
type: worklist
status: current
summary: Open gaps and questions in the reproducible firmware build: provenance not in the image, unrecorded qualification hashes, uncollected bench manifests, the old board path, and pending pins.
---

# Build Worklist

Open items for the model in
[Tag Firmware Build Reproducibility](firmware-reproducibility.md). Delete an
item when it is done.

## Provenance and qualification

- **Qualification does not name the image it measured.** This is the gap that
  matters. `tag_release_check.py` writes `results.json` with a verdict per check
  and the commit of the working tree it ran in, but no field for the SHA-256 of
  the image on the tag. The release names its images by hash and the board
  database names what was programmed, but the qualification between them names
  only a commit. Until it records the hash, the operator writes the release tag
  and image SHA-256 into the result directory by hand
  ([release procedure](../release/release-procedure.md#reading-and-keeping-the-result)).
- **The dirty flag is not carried into the image.** `version.h` defines
  `GIT_DIRTY`, `GIT_DIRTY_STR`, `CHIBIOS_SHA` and `NANOPB_RUNTIME_VERSION`, but no
  firmware source references them, so a returned tag cannot report over the
  monitor that its tree had uncommitted changes; only the build manifest says
  so, and only if it was kept. Left deliberately: a new string in the image
  shifts its layout, and STM32U375 Standby entry depends on layout, so wiring
  these in needs its own commit and its own bench measurement.
- **Bench-built images have nowhere to put their manifests.** A bench build
  writes a manifest into its build tree and nothing collects it: no archive
  location, no naming convention. This matters only if a bench-flashed tag
  reaches the field, which the rule that field tags are programmed from a
  release exists to prevent.
- **Most of the archive is still manual.** The firmware release workflow
  attaches the images and manifests. The `.map`, `project.mk`, linker script,
  `compile_commands.json` and the board's external loader are not collected.

## Generators and pins

- **Move to nanopb 0.4.9.2?** 0.4.9.1 is pinned because it generated the tree's
  existing output. The newer LTS bugfix release is worth taking deliberately
  and separately. The pin lives in three places that must move together: the
  vendored runtime directory, `requirements.txt`'s protobuf (which must match
  the `protoc` that distribution ships), and `NANOPB_URL`/`NANOPB_SHA256` in
  `generated-sources-reproduce.yml`.
- **Record the `config-gen` protobuf version in `tools_text`.** It is pinned in
  `embedded/proto-c/requirements.txt` and identical on every machine by
  construction, so recording it is honest. It was left out so that the manifest
  churn belongs to one commit.
- **Record `fmpp` in the build.** The per-image build manifest does not record
  the `fmpp` version; that is a small addition. Recording it in the board
  `inputs.sha256` is a larger one: it needs `fmpp` and its JRE pinned the way
  `config-gen`'s protobuf is, and the pin and the manifest entry have to land
  together, in a commit that regenerates every board. See
  [decision 0015](../decisions/0015-build-pin-fmpp-and-its-rendering-environment.md).
- **The old board generation path has no reproducibility support.**
  `generate_board_files` -- the static flow that renders `cfg/*.ftl` from a
  checked-in `board.chcfg` -- has no manifest, no committed output and no
  staleness check. `TagSteval`, `bittag-base-jlcpcb-v3` and
  `tag-breakout-base-jlcpcb32-v1` use it (as does `bittag-base-jlcpcb-v2`,
  which is not configured); none is behind a distributed tag. If one ever is, configure reports it as a
  distributed board that does not commit its generated files, and the remedy it
  suggests (create `generated/` and regenerate) does nothing on this path. Port
  the board to `generate_configured_board_files` first.

## Scope

- **Do host tools need any of this?** They are out of scope: they release on a
  different clock and their dependencies are pinned by the vcpkg baseline. But
  the `dataprocessing` path also produces records that outlive their build.
