---
type: proposal
status: proposed
summary: "Give the Tauri applications one shell and one engine toolkit instead of four copies -- what the copies already differ in, which side of each difference wins, what stays app-specific (Tag Programmer's threading), the crates, the phases with the bench test each app needs, and the open decisions."
---

# One shell and one engine toolkit for the Tauri applications

**Proposed 2026-11-08.** Tag Programmer, Tag Monitor, Tag Calibrator and Tag
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
   used by the three tag applications: the `Engine` handle, the loop that
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
| 1. Shell | `tagapp-shell` and `tagapp-shell-build`; move all four applications onto them; Tag Programmer and Tag View gain the close hook and Quit item (see open question 1) | Each app: launch, title, Help > User Guide, Cmd-W and Cmd-Q with and without unsaved work, page reload; Tag Monitor and Tag Calibrator attach and detach a tag |
| 2. Toolkit, Tag Calibrator | `tagapp-engine`; Tag Calibrator moves onto `Engine`, `run_loop`, `Prompts`, `Ready`, `UiLogger` | Its replay tests unchanged; on the bench: attach, stream, collect, write calibration, reload mid-stream |
| 3. Toolkit, Tag Monitor | Tag Monitor moves onto the toolkit; no behaviour change intended | Its oracle and golden tests unchanged; on the bench: attach, configure, Start, Stop, download, erase |
| 4. Toolkit, Tag Programmer | Tag Programmer moves onto the toolkit: `ready`, the prompt queue and `answer`, menus from the engine | Its tests; on the bench: the full program-and-record sequence on one L4 and one U375 tag, with an image and a record file |

The mock engines (`ui/mock-engine.js`) and the screenshots for the manual
change with each page; `screenshots.sh` before and after shows any visual
difference.

## Documents to change

- The tutorial's chapters 10, 11 and 14 (shorter: one shell and one toolkit
  to read, then each application's differences).
- Guide chapters 10 and 11; each application's `design/ui-engine-contract.md`
  and architecture document.
- A decision record when phase 1 is done.

## Open questions

1. **Closing Tag Programmer while it programs a tag.** Today the window
   closes at once and the worker dies with the process, possibly mid-write.
   Proposed: the close hook asks "Programming is in progress; quit anyway?"
   while `worker_busy` is set, and quits at once otherwise.
2. **Tag View.** It shares the shell (phase 1) but not the toolkit: its
   engine has no tag and no prompts of this kind. Proposed: shell only.
3. **Crate names and places.** `tagapp-shell-rs`, `tagapp-shell-build-rs`
   and `tagapp-engine-rs` under `host/libraries/`, beside the other shared
   crates.
4. **Order.** Phase 1 alone removes the class of bug that prompted this; the
   toolkit phases could wait for the next change to each application.
   Proposed: phase 1 now, phases 2 to 4 one at a time when each application
   is next changed.
