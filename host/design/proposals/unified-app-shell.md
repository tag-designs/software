---
type: proposal
status: accepted
summary: "Give the Tauri applications one shell and one engine toolkit instead of four copies -- what the copies already differ in, which side of each difference wins, what stays app-specific (Tag Programmer's threading), the crates, the phases with the bench test each app needs, and the open decisions."
---

# One shell and one engine toolkit for the Tauri applications

**Proposed 2026-11-08; decisions taken the same day (see the end). Phase 1
(the shell) done 2026-11-08; phase 2 (the toolkit, Tag Calibrator and Tag
View) done 2026-11-08; phase 3 (Tag Monitor) done 2026-11-08; phase 4
open
([Phases](#phases)).** Tag Programmer, Tag Monitor, Tag Calibrator and Tag
View each carry their own copy of the same two layers: a Tauri shell
(`app/src/main.rs`, `app/build.rs`, `capabilities/`) and the plumbing of an
engine (the `Engine` handle, the engine loop, the logger, prompts, `ready`).
The copies were written one after another and have drifted; the five
problems listed under *What the drift has cost* were each handled in one copy
and missing from another. This proposes:

1. **One shell crate** used by all four applications: `main`, the two
   commands and two events, the close and Quit handling, App Nap, the user
   guide search, and the page staging in `build.rs`.
2. **One engine toolkit** (no Tauri, so the `core` crates stay toolkit-free)
   used by the three tag applications, and by Tag View for all but prompts: the `Engine` handle, the loop that
   emits the view state when it changes, `ready` with a resend, prompts with
   ids in a queue, and the `log` forwarding.
3. **One page contract** for those pieces: `ready`, `answer {id, ...}`, and
   menus from the `menus` query.

Each application keeps its own model, view state, commands and tag work.
Tag Programmer keeps its threading (engine owns the `Tag`, workers return
closures); the reason is below.

## What the copies differ in today

Read from the tree at `7b217994` (2026-11-08).

### The shell

| | Tag Programmer | Tag Monitor | Tag Calibrator | Tag View |
| --- | --- | --- | --- | --- |
| `main.rs` lines | 159 | 207 | 197 | 129 |
| `build.rs` lines | 101 | 147 | 147 | 145 |
| Lines of `main.rs` differing from Tag Monitor's | 192 | -- | 108 | 180 |
| Window close turned into the engine's `quit` | no: closes at once, even while programming a tag | yes | yes | no |
| macOS Quit item sends `quit` (Cmd-Q) | no | yes (`742f19d9`) | yes (`742f19d9`) | no |
| App Nap opt-out (`stay_awake`) | yes | yes | yes | no (attaches no tag) |
| `core:window:allow-set-title` permission | no, though the page calls `setTitle` | yes | yes | yes |
| User guide search (`user_guide_url`) | yes | yes | yes | yes |
| `build.rs`: page staging under `OUT_DIR`, kit path rewrite, `TAURI_CONFIG` | yes | yes | yes | yes |
| `build.rs`: rpath for `libtagcore_c` | no (links no C++) | yes | yes | yes |

The shared part of `main.rs` is about 120 lines per application (commands,
events, setup, close hook, Quit menu, App Nap, user guide); of `build.rs`,
about 90.

### The engine plumbing

| | Tag Programmer | Tag Monitor | Tag Calibrator |
| --- | --- | --- | --- |
| `engine/mod.rs` (handle, loop, logger) lines | 326 | 301 | 282 |
| Who owns the `Tag` | the engine thread; workers run long jobs and return `Msg::Done` closures | a tag thread; `TagCmd` in, `Report` out | a tag thread; `TagCmd` in, `Report` out |
| First page command | `start`, once (`started` guard) | `ready`: attaches once, resends the whole state every time | `ready`: attaches unless attached, never resends |
| Reloaded page gets the state at once | no | yes | no |
| Prompt storage | one, a new prompt replaces it | a queue | one, a new prompt replaces it |
| Prompt type | raw JSON body | `PromptKind` enum | `PromptKind` enum |
| Answer command | one per kind (`choose_base`, `record_open`, `prompt_cancel`, ...), with `id` since `ae198e14` | `answer {id, button, index?, path?}` | `answer {id, button, index?, path?}` |
| Menus | hard-coded in the page | `menus` query | `menus` query |
| Query timeout | 10 s | 10 s | 10 s (Tag View: 30 s) |
| `UiLogger` and `LOG_LEVEL` | own copy | own copy | own copy |

### What the drift has cost

Each of these was a fix made, or a rule followed, in one copy and missing in
another:

- **Cmd-Q** skipped the unsaved-changes question and the detach in Tag
  Monitor and Tag Calibrator, because Tauri's default macOS Quit bypasses the
  close hook. Fixed twice, by hand, in `742f19d9`. Tag Programmer and Tag View
  have no close hook to route it to.
- **Prompt ids:** Tag Programmer's answers carried none, so a late answer
  could clear a newer prompt (`ae198e14`). The other two checked ids from the
  start.
- **A replaced prompt is lost** in Tag Programmer and Tag Calibrator: raising
  a prompt while another is open discards the first. Tag Monitor queues them.
- **Reload:** only Tag Monitor resends the state on `ready`.
- **Permissions:** Tag Programmer's capability file lacks the set-title
  permission its page uses; the refusal is silent.

## The proposal

### 1. A shell crate: `tagapp-shell`

A library crate, `host/libraries/tagapp-shell-rs`, depending on `tauri` and
the two plugins. An application's `main.rs` shrinks to a description and one
call:

```rust
fn main() {
    tagapp_shell::run(tagapp_shell::App {
        name: "Tag Monitor",
        guide_page: "apps/tagmonitor",
        keep_awake: true,
        close_through_engine: true,
        spawn: |paths| engine::spawn(EngineConfig::from(paths), /* sink given by run */),
        context: tauri::generate_context!(),
    });
}
```

`run` owns what every shell repeats today:

- the `command` and `query` Tauri commands and the `state` and `log` events,
  through a small `Engine` interface (`command(&str, Value)`,
  `query(&str, Value) -> Value`) that every engine handle already has;
- with `close_through_engine`, the close hook and the macOS Quit item, both
  sending `quit` and closing on the engine's `Exit`;
- `stay_awake` when `keep_awake` is set;
- `user_guide_url` from `guide_page`.

`generate_context!` must expand in the application crate (it reads that
crate's `tauri.conf.json`), so the application passes it in.

A build-script half, `tagapp-shell-build` (a separate small crate, used as a
`[build-dependencies]` entry), does the page staging, the kit path rewrite,
`TAURI_CONFIG` and, when asked, the `libtagcore_c` rpath. Each `build.rs`
becomes one call.

One capabilities file, generated or copied by the build helper, gives every
application the same permissions.

### 2. An engine toolkit: `tagapp-engine`

A library crate with no Tauri dependency, `host/libraries/tagapp-engine-rs`,
so a `core` crate can use it and still build and test without a window:

- **`Engine`**, the handle: a command channel, `query` with a reply channel
  and a timeout the application sets (10 s; Tag View's 30 s).
- **`run_loop`**: wait on the channel until the application's next deadline,
  apply the message, fire timers, rebuild the view state, emit it when it
  differs from the last one sent or when a `ready` asked for a resend. The
  application supplies its state type through a small trait (`apply`,
  `next_deadline`, `on_timer`, `view_state`).
- **`Prompts<K>`**: a queue of prompts of the application's kind `K`, each
  with a fresh id; `ask`, `front` for the view state, and `answer(args)`,
  which returns the answered prompt and the parsed `{button, index, path}`, or
  nothing for a stale id. Tag Monitor's queue and its `answer` are the model.
- **`Ready`**: the first-time guard and the resend flag Tag Monitor's
  `ready` has.
- **`UiLogger`**: the `log` forwarding and level setting all three repeat.

### 3. The page contract

- `ready` everywhere (Tag Programmer's `start` is renamed); a reloaded page
  always gets the whole state.
- One `answer {id, button, index?, path?}` command; the kit gets one
  `showPrompt` helper that maps the shared prompt kinds (alert, confirm,
  choose from list, open and save file) to its dialogs. Application-specific
  kinds stay in the page.
- Menus from the `menus` query in all three; Tag Programmer's hard-coded menus
  move into its engine.

### Which side wins

| Difference | Wins | Why |
| --- | --- | --- |
| Close and Quit | Through the engine's `quit` | The only way to ask about unsaved work or an operation in progress and to detach cleanly |
| Prompt storage | Queue (Tag Monitor) | A replaced prompt is a lost message; a queue loses nothing |
| Prompt answers | `answer {id, ...}` (Tag Monitor) | One command, one stale-id rule, one parser |
| First command | `ready` with guard and resend (Tag Monitor) | Reload works; attaching twice is impossible |
| Menus | Engine's `menus` query | The engine already decides what is enabled |
| Threading | Each application's own | See below |

### What stays app-specific: Tag Programmer's threading

Tag Programmer's engine owns its `Tag` and runs probe-rs on workers. Its
programming step detaches tagcore and hands the base to a probe-rs worker in
consecutive statements (`Station::run_programmer`, `engine/session.rs`). With a tag
thread, the detach would become a message and an acknowledgement before the
worker could start, and the session's step machine, the most complicated
engine code in the tree, would have to be rewritten around that wait. The
gain would be uniformity, not behaviour, and the cost is a full
re-qualification of field programming. The toolkit does not require a tag
thread: `run_loop` drives whatever state the application gives it, and Tag
Programmer's `Msg::Done` closures fit as an application message.

## Phases

Each phase leaves every application working; each ends with its bench check.

| Phase | Work | Check |
| --- | --- | --- |
| 1. Shell | `tagapp-shell` and `tagapp-shell-build`; move all four applications onto them; Tag Programmer gains the close hook and Quit item, and asks before closing while programming (decision 1) | Each app: launch, title, Help > User Guide, Cmd-W and Cmd-Q with and without unsaved work, page reload; Tag Monitor and Tag Calibrator attach and detach a tag |
| 2. Toolkit, Tag Calibrator and Tag View | `tagapp-engine`; Tag Calibrator moves onto `Engine`, `run_loop`, `Prompts`, `Ready`, `UiLogger`; Tag View onto all of them but `Prompts` (decision 2) | Tag Calibrator's replay tests unchanged; on the bench: attach, stream, collect, write calibration, reload mid-stream. Tag View: open a log, the compass, print, reload |
| 3. Toolkit, Tag Monitor | Tag Monitor moves onto the toolkit; no behaviour change intended | Its oracle and golden tests unchanged; on the bench: attach, configure, Start, Stop, download, erase |
| 4. Toolkit, Tag Programmer | Tag Programmer moves onto the toolkit: `ready`, the prompt queue and `answer`, menus from the engine | Its tests; on the bench: the full program-and-record sequence on one L4 and one U375 tag, with an image and a record file |

**Phase 1 done, 2026-11-08.** `host/libraries/tagapp-shell-rs` and
`host/libraries/tagapp-shell-build-rs` are in the workspace and all four
applications run on them: each `main.rs` describes its application (`App`)
and builds its engine, each `build.rs` is one `tagapp_shell_build::build`
call, and the per-application `capabilities/default.json` files are gone in
favour of the one in `tagapp-shell-build` (which gives Tag Programmer
`core:window:allow-set-title`). Tag Programmer's engine gained `quit`,
`quit_anyway`, the `confirmQuit` prompt and `Event::Exit`; it asks only while
a session is in its `Programming` step (probe-rs erasing, programming and
verifying), the one step a quit would interrupt mid-write. Bench-checked by the user: every application's
launch, title, Help > User Guide, Cmd-W, Cmd-Q and page reload; Tag
Programmer's Quit anyway during programming; Tag Monitor and Tag Calibrator
attach and detach. The shell is described in the crates' READMEs
([`tagapp-shell`](../../libraries/tagapp-shell-rs/README.md),
[`tagapp-shell-build`](../../libraries/tagapp-shell-build-rs/README.md)),
guide chapter 10 and tutorial chapter 10, and recorded in decision
[0046](../../../docs/decisions/0046-host-shared-app-shell.md).

**Phase 2 done, 2026-11-08.**
`host/libraries/tagapp-engine-rs` (package `tagapp-engine`, no Tauri
dependency) is in the workspace and documented in its
[README](../../libraries/tagapp-engine-rs/README.md). It differs from the
sketch above in names only: the trait is `EngineState` (`command`, `query`,
`view_state`, and optional `message`, `next_deadline`, `fire_timers`,
`take_republish`, `take_exit`), the loop is started by `spawn`, and
`channel` makes the `Engine`, a `Poster` for the application's own messages
and the engine thread's inbox. `Engine` is not generic over the
application's message type. Tag Calibrator's `engine/mod.rs` is now its
`spawn` and re-exports, its tag thread posts `Report`s through the `Poster`,
and `Calibrator` implements `EngineState`; Tag View's `Viewer` implements it
with `Msg = ()` and a 30 s query timeout. Three behaviour changes follow:

- **Tag Calibrator queues prompts.** A prompt raised while another is up
  waits its turn instead of replacing it.
- **Tag Calibrator's `ready` starts once and resends every time.** A page
  reload no longer re-attaches or restarts the replay; it gets the whole
  state again.
- **Tag View's `ready` starts once and resends every time.** A page reload
  no longer reopens the startup log and preferences over what is open; it
  gets the whole state again.

Bench-checked by the user: Tag Calibrator attach, stream, collect, write
and read back the calibration, and quit while streaming; Tag View open a
log, the compass, print, open a second log and quit. The page-reload step
was dropped: the release builds have no way to reload a page by hand, and
`Ready` is covered by the toolkit's unit tests.

**Phase 3 done, 2026-11-08.** Tag Monitor's core runs
on the toolkit. Its `engine/mod.rs` is now `EngineConfig`, `QUERY_TIMEOUT`
(10 s) and a `spawn` that makes the toolkit's channel, starts the tag thread
with the `Poster`, and starts the toolkit's loop on a `Monitor`; `Engine`,
`Event`, `LOGGER` and `UiLogger` are re-exported from `tagapp-engine`. Its
own handle, `Msg`, loop and logger are gone. `Monitor` keeps its prompts in
`Prompts<PromptKind>` (the stale-id rule is now the toolkit's), guards
`ready` with `Ready`, and implements `EngineState` with the tag thread's
`Report` as its message; the tag thread posts its reports through the
`Poster`. No behaviour change is intended: Tag Monitor already queued its
prompts and resent the state on every `ready`, which is where the toolkit's
versions came from. Its unit, oracle and golden tests pass unchanged. Bench-checked by the
user: attach, configure, Start, Stop, download, erase, and quit with
unsaved changes. The documents changed with
it: the toolkit's README, Tag Monitor's README, architecture and contract,
guide chapters 02, 10, 11, 12, 14 and 15, and tutorial chapters 01, 02, 10,
11, 13 and 14.

The mock engines (`ui/mock-engine.js`) and the screenshots for the manual
change with each page; `screenshots.sh` before and after shows any visual
difference.

## Documents to change

- The tutorial's chapters 10, 11 and 14 (shorter: one shell and one toolkit
  to read, then each application's differences).
- Guide chapters 10 and 11; each application's `design/ui-engine-contract.md`
  and architecture document.
- A decision record when phase 1 is done.

## Decisions (2026-11-08)

1. **Tag Programmer asks before closing while it programs a tag.** While a
   session is in its programming step (erase, program and verify; a test or
   read-back is not interrupted mid-write), a window close or Cmd-Q asks
   "Programming is in progress. Quitting now can leave the tag with a partial
   image. Quit anyway?", defaulting to No; otherwise closing works as usual.
   The choice stays with the user: a stuck job must not trap the program.
2. **Tag View takes the shell and the toolkit without prompts.** Its engine
   already repeats the `Engine` handle (with a 30 s query timeout), the
   emit-on-change loop, `ready` and the `UiLogger`, so it moves onto those in
   phase 2. It has no prompts of this kind, so it does not use `Prompts`; in
   the shell it keeps a plain close (no unsaved work, no tag to detach) and no
   App Nap opt-out.
3. **Crates:** `tagapp-shell-rs`, `tagapp-shell-build-rs` and
   `tagapp-engine-rs` under `host/libraries/`.
4. **Order:** phase 1 now; phases 2 to 4 one application at a time, when each
   is next changed.
