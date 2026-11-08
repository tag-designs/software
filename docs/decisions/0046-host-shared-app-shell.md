---
type: decision
status: accepted
summary: "The four Tauri applications share one shell (tagapp-shell) and one build helper (tagapp-shell-build) with one capabilities file; Tag Monitor, Tag Calibrator and Tag Programmer close through their engine's quit, Tag Programmer asking only while it programs a tag; Tag View keeps a plain close."
---

# 0046. Host: one shell for the Tauri applications

Date: 2026-11-08

## Context

Tag Monitor, Tag Calibrator, Tag Programmer and Tag View were each given
their own Tauri shell -- `main.rs`, `build.rs` and a capabilities file --
copied from whichever application came before and then changed separately.
By November 2026 the copies differed in ways no one had chosen: Tag
Programmer's window closed without asking its engine, so a close or Cmd-Q in
the middle of programming left a partial image; it also lacked the
`core:window:allow-set-title` permission the others had; and a fix to one
shell (Cmd-Q on macOS reaching the engine, commit 742f19d9) had to be made
two or three times.

The plan, and the engine toolkit that follows it, is
[One Shell and One Engine Toolkit](../../host/design/proposals/unified-app-shell.md).

## Decision

- **One shell crate, `tagapp-shell`** (`host/libraries/tagapp-shell-rs`),
  owns everything the four shells repeated: the `command` and `query` Tauri
  commands, the `state` and `log` events, the macOS App Nap opt-out, the user
  guide lookup, the dialog and opener plugins, and closing through the engine.
  Each application's `main.rs` describes the application in an `App` value and
  builds its engine; nothing else.
- **One build helper, `tagapp-shell-build`** (`host/libraries/tagapp-shell-build-rs`),
  stages the page and the UI kit, writes the one capabilities file every
  application uses, sets the tagcore_c rpath where an application needs it,
  and runs `tauri_build`. Each `build.rs` is one call.
- **Closing goes through the engine** for the three applications that attach
  to a tag (`close_through_engine`): a window close, and on macOS the app
  menu's Quit, become the engine's `quit`, and the window closes when the
  engine says so. Tag View, with no tag and no unsaved work, closes at once
  and does not opt out of App Nap.
- **Tag Programmer asks only while it programs a tag.** During a session's
  programming step a quit raises "Programming is in progress. Quitting now
  can leave the tag with a partial image. Quit anyway?", defaulting to Keep
  Programming. At any other time it quits at once. The choice stays with the
  user, so a stuck job cannot trap the program.
- **The app crates still depend on `tauri` and the two plugins directly.**
  `generate_context!` expands in the crate that calls it, and Tauri resolves
  plugin permissions only for a crate's direct dependencies.

## Consequences

- A shell fix is made once and reaches every application.
- Tag Programmer gained the set-title permission and the close hook.
- The engine toolkit (`tagapp-engine`, phases 2 to 4 of the proposal) is not
  part of this decision; each application moves onto it when it is next
  changed.
- Bench-checked on 2026-11-08: each application's launch, title, user guide,
  Cmd-W, Cmd-Q and reload; Tag Programmer's Quit anyway during programming;
  Tag Monitor and Tag Calibrator attach and detach.
