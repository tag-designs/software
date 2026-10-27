---
type: decision
status: accepted
summary: Tag Monitor was ported to Rust as qtmon-rs (shipped as tagmonitor beside qtmonitor) on Tag Programmer's pattern - a toolkit-free engine with a tag thread, a Tauri page on the shared kit, every dialog an engine prompt - with the configuration form reproducing qtmonitor's panels and Qt widget behaviour exactly, checked against all 710 probe goldens, and downloads written by the C++ writer through tagcore_c.
---

# 0037. Host: Tag Monitor in Rust (qtmon-rs)

Date: 2026-10-27

## Context

[0035](0035-host-tagprogram-rs.md) ported qtprogram to Rust, with a UI
approach meant to serve the other host applications. qtmonitor was then
restructured so that a port would be a translation rather than a rewrite
([0036](0036-host-qtmonitor-engine-model-view.md)): a Qt-free core, a tag
session on its own thread, and a window that only draws. The user asked
whether Tag Monitor could be turned into a Rust implementation like Tag
Programmer, on its own branch, and later asked for it to ship.

Two things made Tag Monitor harder than Tag Programmer:

- **The configuration form.** What Start sends depends on qtmonitor's
  panels and on how Qt's widgets behave: date boxes that clamp to a
  minimum and re-run the panels' whole-hour handlers on every change,
  decimal spin boxes that round half away from zero and re-round their
  limits when their decimals change. A port of the panel code alone would
  send different bytes in edge cases.
- **Downloads.** Tag Monitor writes SQLite logs. The writer, its schema and
  its per-tag decoding are C++ (`SqliteTagLogWriter`).

## Decision

- **Port qtmonitor's behaviour module for module.** `core/src/model` ports
  `qtmon/core` (the command rule, start checks, tag rules, monitor model,
  configuration document, menus) and the configuration panels. Each Rust
  module names the C++ file it ports.
- **Two threads, as qtmonitor has after 0036.** A tag thread owns the
  `tagcore::Tag`, polls status, runs commands in order and runs downloads;
  the engine thread owns the model, the document, the prompts and the view
  state, and never waits on the tag.
- **Every dialog is an engine prompt.** qtmonitor's modal dialogs (choose
  a base, Start Anyway, erase, unsaved changes, the file pickers, which tag
  type a file is for) are prompts in the view state that the page answers
  by id. The engine owns what happens next, so Save after "unsaved changes"
  continues with what it interrupted. The page holds no rules
  ([contract](../../host/applications/qtmon-rs/design/ui-engine-contract.md)).
- **Model Qt's widgets, not only the panels.** `configform.rs` emulates the
  date, spin and radio widgets the panels rely on, and the page draws the
  form generically from the engine's description of each control.
- **Write downloads with the C++ writer, through `tagcore_c`** and the
  `taglogwriter` crate, as the Rust `tag-dwnld` does. Rust may depend on
  C++; there is one writer and one schema.
- **Ship inside the host packages as `tagmonitor`, beside qtmonitor**, with
  `tagcore_c`: linked statically into the macOS executable, installed as a
  shared library on Linux (`tag_tools/lib`) and Windows (beside the program
  in `tag_tools/lib`, started by the GUI launcher). `BUILD_RUST_APPS` now
  builds `tagcore_c`
  ([build and packaging](../../host/applications/qtmon-rs/design/build-and-packaging.md)).
- **Name bases by identity.** With several bases, each is asked the tag
  bases' identify command (which sends nothing to the target) and listed
  with its board, firmware and ST-LINK serial. qtmonitor lists only USB
  addresses.

## Alternatives considered

- **Port only the panels' code.** Simpler, but the probe showed sent bytes
  depend on widget clamping and rounding order; the edge cases (negative
  epochs, thresholds on a half, limits re-rounded after a range change)
  would have differed.
- **A per-tag hand-written form in the page.** It would put rules in the
  UI, against the split 0035 chose, and duplicate the panel table.
- **Port the SQLite writer to Rust.** Rejected for the same reasons as for
  `tag-dwnld`: a second writer would have to track every schema change.
- **Ship without Download** (no `tagcore_c` in the packages). Offered; the
  user chose to ship with it.
- **Build only, unpackaged.** Offered; the user chose to ship.

## Evidence

- **Oracles:** the command availability table, the start checks and the
  menu layout match qtmonitor's committed oracle files
  (`host/applications/qtmon/test/`), and `qtmon_core_check`'s assertions
  pass in Rust (`core/tests/`).
- **Configuration probe:** all 710 cases in `qtmon/test/cases/` match the
  reference build's goldens: the bytes Start sends, the document, the
  panels shown, the dialogs, the open result, and every control's value,
  range, step, decimals and enabled state (`core/tests/config_probe.rs`).
  Deliberately breaking the rounding made 22 cases fail.
- **A tag cycle:** `core/examples/session_check.rs`, the Rust counterpart of
  `qtmon_session_check`, passed on an IMUTag (U375) through the engine:
  attach, sync, self-test, read, start, 60 s run, stop, download (37 pages,
  a complete log), erase, detach.
- **Use:** the user ran the app and found it "pretty close".

## Consequences

- Two Tag Monitors ship until qtmonitor is retired. The configuration probe
  goldens hold both to the same bytes; a change to qtmonitor's panels must be
  made in `configform.rs` too, and the goldens regenerated from the
  reference build.
- `tagcore_c` is no longer experimental: it is built with the Rust apps and
  packaged for Tag Monitor.
- The UI kit gained submenus, popup menus and setting the window title,
  which Tag Programmer may also use.
- qtmonitor's fixture mode and screenshot capture were not ported; the
  documentation screenshots still come from qtmonitor.
