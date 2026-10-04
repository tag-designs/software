---
type: procedure
status: current
summary: Where each kind of developer document lives, the front matter it carries, and how the index and portal are generated.
---

# Developer Documentation Guide

Every developer document has exactly one **type**, lives in the place that type
belongs, and carries front matter saying what it is. The index, the portal
sidebar and the portal's staging list are all generated from that front matter
by [`docs/tools/docs.py`](tools/docs.py), so there is no list to keep in sync by
hand.

This guide covers Markdown. API contracts are documented in Doxygen comments;
the standard for those is in [AGENTS.md](../AGENTS.md#documentation-standards-c--c--cmake).

## Document types

Most documents that read as stream of consciousness are two or three of these
types in one file. Split them.

| Type | Answers | Tense | Lifecycle | Where it lives |
| --- | --- | --- | --- | --- |
| `readme` | What is here, how do I build and use it | Present | Edited in place, always current | `README.md` next to the code |
| `design` | How it works now, and why | Present | Rewritten when the code changes; no history inside | `design/` next to the owning code |
| `decision` | What was chosen, what was rejected, the evidence | Past | Written once, then frozen; superseded rather than edited | [`docs/decisions/`](decisions/), numbered |
| `proposal` | What is planned but not built | Future | Becomes a `design` doc and a `decision` when built; marked `historical` or deleted if dropped | `design/proposals/` next to the owner |
| `investigation` | What was measured or tried while chasing a fault | Past, dated | Append while open, then `closed` with a link to the decision or fix | `design/investigations/` next to the owner, or [`docs/investigations/`](investigations/) when cross-cutting |
| `procedure` | How to do a repeatable task | Imperative | Edited in place | [`docs/bench/`](bench/), [`docs/release/`](release/), or the owning tool's README |
| `results` | Raw measurements with the build and conditions | Past, dated | Append-only | Beside the procedure that produced them |
| `worklist` | What is still open | Present | Items deleted when done, never struck through | `TODO.md` in the owning directory |
| `index` | A list of other documents | — | Generated | [`docs/index.md`](index.md) |

## Rules

1. **A design doc describes the shipped system only.** "We first tried X" goes
   to a decision record or an investigation, and the design doc keeps a
   one-line link to it.
2. **The first paragraph states the current answer**, for example "Terminal
   sleep is Stop 3; Standby is not used on U375."
3. **One fact, one home.** Link rather than restate. Measured numbers live in a
   `results` log; other documents quote them with a link.
4. **Lowercase, hyphenated file names.** `README.md`, `TODO.md` and the
   generated `BUILD_SOURCES.md` are the only exceptions.
5. **Platform before family before target.** Behaviour shared by every tag on
   an MCU belongs under `embedded/tags/common/`; a family's `design/` holds only
   what the family does differently; a target gets its own `design/` only when
   it has forked from its family.
6. **The user manual is separate.** `host/docs/src/` is packaged for end users
   and has its own MkDocs site. Developer material does not go there.

## Front matter

Every developer document starts with:

```yaml
---
type: design            # one of the types above
status: current         # see below
summary: One sentence saying what a reader gets from this document.
---
```

| Status | Meaning |
| --- | --- |
| `current` | Describes the tree as it is |
| `proposed` | A proposal not yet built |
| `accepted` | A decision record in force |
| `superseded` | Replaced; add `superseded-by: <path>` |
| `open` | An investigation still in progress |
| `closed` | An investigation finished |
| `historical` | Kept for the record; do not quote it as current |

Optional fields: `title` (overrides the first heading in the index),
`superseded-by`, and `last-verified` (a commit at which the document was last
checked against the code).

## Where cross-cutting documents go

`docs/` holds only material with no single owning directory:

| Directory | Holds |
| --- | --- |
| [`docs/architecture/`](architecture/) | How the host, the tags and the data fit together |
| [`docs/shared/`](shared/) | Contracts both host and firmware depend on: `proto/`, `include/`, log formats, the monitor protocol |
| [`docs/build/`](build/) | The build and reproducibility model |
| [`docs/release/`](release/) | Qualifying, programming and publishing a release |
| [`docs/bench/`](bench/) | Hardware procedure: verifying a change, power testing, debugging, capturing a tag |
| [`docs/decisions/`](decisions/) | Numbered decision records, `NNNN-short-title.md` |
| [`docs/investigations/`](investigations/) | Cross-cutting investigations, `YYYY-MM-short-title.md` |

Everything else lives next to the code it describes.

## Commands

```sh
docs/tools/docs.py check    # front matter, names, relative links, index freshness
docs/tools/docs.py index    # regenerate docs/index.md after adding or retitling a document
cmake --build <build-dir> --target developer_docs   # build the portal
```

Link to source files, scripts and other documents with ordinary relative
links; `check` verifies they resolve in the repository. When the portal is
built, a Markdown page a developer document links to (a user-manual page, say)
is published with it, and links to files the portal does not publish (sources,
scripts, JSON) are pointed at the file on GitHub's `main` branch.

Run `check` before committing a documentation change. Adding a document needs
nothing more than its front matter and `docs.py index`: the portal stages every
document `docs.py list` reports and builds its sidebar from the same data.
