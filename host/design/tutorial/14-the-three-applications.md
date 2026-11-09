---
type: design
status: current
summary: "Tag Programmer and Tag Calibrator read against Tag Monitor - what each does, its core crate's types and threads, its page, and where it follows or departs from the pattern, with the shared engine toolkit (tagapp-engine) all three run on - ending with a side-by-side table of the three."
---

# 14. Tag Programmer, Tag Monitor and Tag Calibrator compared

The three Tauri applications share one shape: a toolkit-free `core` crate that
holds every rule and all state, a small Tauri `app` crate, and a web page on
the UI kit. [Chapter 10](10-tauri-shell.md) and [chapter 11](11-engine-and-model.md)
read that shape in Tag Monitor, and [chapter 12](12-ui-kit-and-pages.md) read
its page. This chapter reads the other two against it: Tag Programmer
(`host/applications/tagprogram-rs`), which came first and is an earlier
variant of the pattern, and Tag Calibrator (`host/applications/tagcalibrate-rs`),
which came last and follows Tag Monitor closely but streams sensor data and
draws in 3D. After reading it you can open either app's `core/src/engine` and
`ui/app.js` and know which parts are the pattern and which are the app's own.

What each application does for its user is in
[chapter 11 of the guide](../guide/11-applications.md); the decisions are
[0035](../../../docs/decisions/0035-host-tagprogram-rs.md) (Tag Programmer),
[0037](../../../docs/decisions/0037-host-tagmonitor-rs.md) (Tag Monitor) and
[0039](../../../docs/decisions/0039-host-tagcalibrate-rs.md) (Tag Calibrator).

## Order of creation, and what was copied

Tag Programmer was ported from qtprogram first. Tag Monitor followed, adding
closing through the engine and a settings file, and Tag Calibrator copied
Tag Monitor's engine shape. So when the two older apps differ, Tag Monitor's
way is the later one, and Tag Calibrator usually agrees with it. The Tauri
shells no longer differ in code: all three call the shared `tagapp-shell`
crate ([chapter 10](10-tauri-shell.md)), and each `main.rs` only describes
its application and builds its engine's configuration. The engines have
converged the same way: all three take their handle, loop, prompt queue,
`ready` guard and logger from the shared engine toolkit, `tagapp-engine`
([below](#the-engine-toolkit-underneath)), and Tag Programmer, the last to
move onto it, still runs it in its own way, with no tag thread.
The differences are listed in the table at the end; the sections
below explain the ones that change how the code reads.

## Tag Programmer

### What it does

It programs and tests a batch of tags through a USB base and records each one
in a CSV file: qtprogram's station, with the same checks, texts and record,
but programming through probe-rs (by way of `tagprobe`,
[chapter 7](07-programming-tags-and-bases.md)) instead of
STM32CubeProgrammer, and downloading firmware releases from GitHub. Its engine
is documented in
[tagprogram-rs/design/engine.md](../../applications/tagprogram-rs/design/engine.md),
its protocol in
[design/ui-engine-contract.md](../../applications/tagprogram-rs/design/ui-engine-contract.md).

### One engine thread that owns the tag, and workers that return closures

The largest difference is the threading. Tag Monitor has a dedicated tag
thread that owns `tagcore::Tag` and talks to the engine with `TagCmd` and
`Report` messages. Tag Programmer has **no tag thread**: the engine thread
owns the `Tag`, because the monitor calls it makes (attach, status, stop,
erase, self-tests, SetRtc) are quick, as they were on qtprogram's GUI thread.
Work that takes seconds (listing and downloading releases, the SWD connect
check, programming, reading flash back) runs on a short-lived **worker
thread**, and the worker's result comes back as a closure that the engine
thread runs on its state.

```mermaid
flowchart LR
  page["page (JS)"] -- "command / query" --> eng["engine thread<br/>Station, owns Tag"]
  eng -- "spawn_work(work, apply)" --> w["worker thread<br/>probe-rs, HTTP"]
  w -- "Done closure, through a Poster" --> eng
  eng -- "state / log events" --> page
```

The engine thread's message type says it.
[tagprogram-rs/core/src/engine/mod.rs:90](../../applications/tagprogram-rs/core/src/engine/mod.rs#L90):

```rust
/// A worker's result or progress: a closure applied to the station on the
/// engine thread. The station also posts one to itself to defer opening the
/// record until after the first view is published (`Station::begin_checks`).
pub(crate) type Done = Box<dyn FnOnce(&mut Station) + Send>;
```

> **Rust:** `Box<dyn FnOnce(&mut Station) + Send>` is a heap-allocated,
> type-erased callable that takes `&mut Station`, may be called once, and
> may be sent to another thread: roughly a move-only
> `std::function<void(Station&)>`. Sending *code* rather than data is how a
> worker affects state it cannot touch: the `Station` lives on the engine
> thread and no one else holds a reference to it. The nearest Qt idiom is
> `QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)`. `type
> Done = ...` is a type alias, C++'s `using Done = ...`.

The engine itself is the shared toolkit's, as in the other two apps; `Done`
is the application message it carries, where Tag Monitor's is the tag
thread's `Report`. `spawn`,
[mod.rs:131](../../applications/tagprogram-rs/core/src/engine/mod.rs#L131):

```rust
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let (engine, poster, inbox) = tagapp_engine::channel(QUERY_TIMEOUT, sink);
    tagapp_engine::spawn("tagprogram-engine", Station::new(config, poster), inbox);
    engine
}
```

Compare Tag Calibrator's `spawn` ([below](#the-core-tag-monitors-two-threads-over-a-c-session)):
there is no tag thread to start, so the `Poster` goes to the `Station`
itself, which hands clones of it to its workers. `QUERY_TIMEOUT` is 10 s.
`Station::new` runs on the caller's thread, loading the settings file and
the cached release list, and the station then moves into the engine thread.

The station is handed to the loop through the toolkit's trait,
[station.rs:312](../../applications/tagprogram-rs/core/src/engine/station.rs#L312)
(doc comments dropped):

```rust
impl EngineState for Station {
    type Msg = Done;

    fn command(&mut self, cmd: &str, args: &Value) {
        Station::command(self, cmd, args);
    }
    // query, view_state, next_deadline and fire_timers likewise
    fn message(&mut self, done: Done) {
        done(self);
    }
    fn take_republish(&mut self) -> bool {
        self.ready.take_republish()
    }
    fn take_exit(&mut self) -> bool {
        std::mem::take(&mut self.exit)
    }
}
```

`message` simply calls the closure on the station. The loop, which
[chapter 11](11-engine-and-model.md#the-engine-loop) reads, waits for the
next message or timer deadline, applies it, fires the timers, and sends the
view state when it changed or a `ready` asked for it. Its exit step is a
flag: once the station sets `exit` (a confirmed quit, below), `take_exit`
returns true once, the loop sends `Event::Exit`, and the shell closes the
window.

> **Rust:** `std::mem::take(&mut self.exit)` returns the flag's value and
> leaves `false` (the type's default) in its place: a read-and-clear in one
> call, so the loop sees each exit once.

The worker launcher, [station.rs:497](../../applications/tagprogram-rs/core/src/engine/station.rs#L497)
(doc comment dropped; it warns that `apply` "sees the station as it is when
the message is handled, not as it was when the work began"):

```rust
    pub(crate) fn spawn_work<T: Send + 'static>(
        &self,
        work: impl FnOnce() -> T + Send + 'static,
        apply: impl FnOnce(&mut Station, T) + Send + 'static,
    ) {
        let tx = self.tx.clone();
        std::thread::spawn(move || {
            let done: Done = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(work)) {
                Ok(result) => Box::new(move |s: &mut Station| apply(s, result)),
                Err(panic) => {
                    let what = panic
                        .downcast_ref::<&str>()
                        .map(|s| (*s).to_owned())
                        .or_else(|| panic.downcast_ref::<String>().cloned())
                        .unwrap_or_else(|| "unknown panic".to_owned());
                    Box::new(move |s: &mut Station| s.worker_panicked(&what))
                }
            };
            tx.post(done);
        });
    }
```

`self.tx` is the station's `Poster<Done>`. `work` runs on the new thread
and produces a `T`; `apply` is packaged with that `T` into a `Done` closure,
posted, and runs later on the engine thread.

> **Rust:** `spawn_work<T: Send + 'static>` is generic over the result type;
> the *bounds* (like a C++20 concept) say a `T` may move to another thread
> and borrows nothing that could die first. `impl FnOnce() -> T + Send +
> 'static` accepts any closure of that shape, monomorphised as a template
> would be. `move ||` makes the closure take ownership of what it captures,
> which a closure crossing threads must.

> **Rust:** A panic in a worker would normally end only that thread, and
> `apply` would never run, leaving `worker_busy` set for good.
> `catch_unwind` turns the panic into an `Err` value (like a `catch (...)`
> around the call), and `downcast_ref::<&str>()` / `::<String>()` is a
> checked `dynamic_cast` of the panic's payload to recover its message. The
> engine then runs `worker_panicked`, which clears the busy flags and ends a
> running session FAILED.

The one exception to "nothing needs a lock" is the firmware catalog. The
station holds `catalog: Arc<Catalog>`, shared with the workers that refresh it
from GitHub and download into it, and the catalog guards its own fields.
[model/catalog.rs:292](../../applications/tagprogram-rs/core/src/model/catalog.rs#L292):

```rust
pub struct Catalog {
    /// Root of the cache; see the module documentation for its layout.
    cache_dir: PathBuf,
    /// The releases offered, newest first: the last listing plus any cached
    /// release it lacks.
    releases: Mutex<Vec<FirmwareRelease>>,
    /// The tag of the release being fetched, so at most one download runs.
    fetching: Mutex<Option<String>>,
}
```

and its refresh hands a clone of the `Arc` to a worker,
[station.rs:1408](../../applications/tagprogram-rs/core/src/engine/station.rs#L1408):

```rust
        let catalog = self.catalog.clone();
        self.spawn_work(
            move || catalog.refresh(),
            |s, result| {
                s.refreshing = false;
                if let Err(e) = result {
                    warn!("Could not list firmware releases: {e}");
                    s.refresh_error = Some(e);
                }
                s.catalog.load();
```

> **Rust:** `Arc<Catalog>` is a thread-safe reference-counted pointer
> (`std::shared_ptr`); `.clone()` copies the pointer, not the catalog. An
> `Arc` gives only shared (`const`) access, so a `Catalog` that changes
> keeps its mutable parts in `Mutex`es and its methods take `&self`. This is
> *interior mutability*: the C++ equivalent is a class with `mutable
> std::mutex` members and `const` methods.

The station also posts a `Done` closure **to itself**, to run something on
the next pass of the loop, after the current view has been published. At
start-up it opens the record CSV this way, because on macOS the first `open()`
under Documents blocks until the user answers the system's folder-access
prompt; deferring it lets the window draw first, with the Record row saying
what is happening. [station.rs:846](../../applications/tagprogram-rs/core/src/engine/station.rs#L846):

```rust
        self.tx.post(Box::new(move |s: &mut Station| {
            let mut record = RecordLog::new();
            match record.open(Path::new(&path)) {
                Ok(()) => s.record = Some(record),
                Err(e) => warn!("{e}"),
            }
            s.check_record();
            if s.record.is_none() {
                s.raise_record_prompt();
            }
        }));
```

### The Station

`Station` ([station.rs:215](../../applications/tagprogram-rs/core/src/engine/station.rs#L215))
is qtprogram's `Station` plus the parts of its `MainWindow` that held state.
Its `impl` is spread over three files: `station.rs` (checks, base, attach,
polling, firmware choice, record, prompts), `session.rs` (the programming
session) and `view.rs` (the view state, the prompt's JSON and the menus).

> **Rust:** A type's methods may be defined in several `impl Station { ... }`
> blocks in different files of the same crate. It is how one large class is
> split by subject without a partial-class mechanism.

The parts worth knowing:

| Field or item | What it is |
| --- | --- |
| `checks: Vec<CheckStatus>` | the eight Station rows, indexed by `enum Check { Programmer = 0, Base, Swd, Supply, Tag, TagState, Firmware, Record }`; each has a `Level` (Pending, Ok, Warn, Fail) and a text |
| `tag: Tag` | the monitor link, used directly on this thread |
| `worker_busy: bool` | a worker owns the base; polls, attaches and Choose Base wait |
| `base_poll`, `status_poll`, `attach_retry` | timer deadlines: 1 s while no base, 400 ms while attached, 200 ms retries |
| `session: Option<SessionState>` | the current or last programming session |
| `prompts: Prompts<PromptKind>` | the questions waiting for the page, in the toolkit's queue |
| `ready: Ready` | the toolkit's `ready` guard |
| `catalog: Arc<Catalog>` | the release list, shared with workers |

`command` ([station.rs:664](../../applications/tagprogram-rs/core/src/engine/station.rs#L664))
is one `match` on the command name, with two small closures to read
arguments ([station.rs:665](../../applications/tagprogram-rs/core/src/engine/station.rs#L665)):

```rust
        let s = |k: &str| args.get(k).and_then(Value::as_str).unwrap_or("").to_owned();
        let b = |k: &str| args.get(k).and_then(Value::as_bool).unwrap_or(false);
```

A missing or mistyped argument reads as `""` or `false`, and an unknown
command is logged as a warning: nothing is returned to the page, which matches
the shell's `command` returning `()` ([chapter 12](12-ui-kit-and-pages.md#kitcreateengine-tauri-or-the-mock)).

The page's first command is `ready`, as in the other two apps. Its arm in
`command`, [station.rs:668](../../applications/tagprogram-rs/core/src/engine/station.rs#L668):

```rust
            "ready" => {
                if self.ready.ready() {
                    self.begin();
                }
            }
```

and what it starts the first time,
[station.rs:793](../../applications/tagprogram-rs/core/src/engine/station.rs#L793):

```rust
    fn begin(&mut self) {
        if !self.settings.contains(key::AUTO_ATTACH) {
            self.ask(PromptKind::AutoAttach);
            return;
        }
        self.begin_checks();
    }
```

`Ready::ready()` is true only the first time, so a second `ready` neither
asks the first-run question again nor re-runs the checks; but every `ready`
sets the republish flag, so a reloaded page gets the whole state at once.
Before Tag Programmer moved onto the toolkit (2026-11-09), its first command
was a one-shot `start` with no republish, and a reloaded page waited for the
next change. The test `ready_starts_once` in `station.rs` checks both halves.

### Programming: tagprobe on a worker, progress as closures

A Program session stops the tag through its monitor, has the tag's own
firmware erase a held log, then detaches and programs with probe-rs on a
worker. [session.rs:723](../../applications/tagprogram-rs/core/src/engine/session.rs#L723),
the end of `run_programmer`:

```rust
        let driver = self.driver();
        let erase = self.probe_erase(&image);
        info!("probe-rs: {chip}, {}, erase {erase:?}", driver.name());
        let tx = self.sender();
        self.worker_busy = true;
        self.spawn_work(
            move || {
                tagprobe::program(&base, driver, chip, &image.elf_path, erase, |p| {
                    tx.post(Box::new(move |s: &mut Station| s.program_progress(p)));
                })
            },
            |s, result| {
                s.worker_busy = false;
                match result {
                    Err(e) => {
                        error!("{e}");
                        s.finish(
                            &format!("PROGRAM_FAILED: {e}"),
                            &format!(
                                "Programming failed ({e}). The probe's messages are in the log."
                            ),
                        );
                    }
                    Ok(()) => {
                        s.set_step(Step::Connecting, "Connecting to the new image");
                        s.start_timer();
                    }
                }
            },
        );
```

Earlier in the same function, `self.tag.detach()` runs first: the monitor
link and probe-rs must never hold the base at once (the hazard list in
[design/engine.md](../../applications/tagprogram-rs/design/engine.md#hazards)).
`tagprobe::program` calls its `on_progress` callback synchronously, on the
worker, from inside probe-rs's flash loop, with a `tagprobe::Progress`
([tagprobe-rs/src/lib.rs:426](../../libraries/tagprobe-rs/src/lib.rs#L426)):
`Step(&'static str)` ("Erasing", "Programming", "Verifying", "Reading back")
or `Bytes { done, total }`. Each event becomes its own `Done` closure,
posted through the station's `Poster` (`self.sender()` above is a clone of
it), so the station is updated on its own thread. [session.rs:758](../../applications/tagprogram-rs/core/src/engine/session.rs#L758):

```rust
    fn program_progress(&mut self, p: Progress) {
        let Some(s) = self.session.as_mut().filter(|s| s.running) else {
            return;
        };
        match p {
            Progress::Step(name) => {
                info!("probe-rs: {name}");
                s.progress = None;
            }
            Progress::Bytes { done, total } => s.progress = Some((done, total)),
        }
    }
```

> **Rust:** `let Some(s) = expr else { return; };` is *let-else*: bind `s`
> if the pattern matches, otherwise run the `else` block, which must leave
> the function. It replaces `if (!p) return;` followed by a dereference.
> `.filter(|s| s.running)` turns `Some` into `None` when the predicate
> fails, so a progress event that arrives after the session ended is
> dropped.

The view turns `progress` into what the page's bar needs,
[view.rs:205](../../applications/tagprogram-rs/core/src/engine/view.rs#L205):

```rust
        let progress = if s.running && s.step != Step::PowerCycle {
            match s.progress {
                Some((done, total)) => json!({ "done": done, "total": total }),
                None => json!("busy"),
            }
        } else {
            Value::Null
        };
```

So the whole progress path is: probe-rs flash loop, `on_progress` on the
worker, a posted `Done` closure, `SessionState.progress` on the engine thread,
a changed view state, a `state` event, and the page's `Session` component
drawing a `Progress` bar (`null` hidden, `"busy"` indeterminate, `{done,
total}` a fraction). Every progress event rebuilds the view; the change check
keeps it from being sent when nothing visible moved.

### The session step machine

The session's steps are qtprogram's `ProgramSession::Step`.
[session.rs:93](../../applications/tagprogram-rs/core/src/engine/session.rs#L93):

```rust
/// The session's steps (`ProgramSession::Step`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Step {
    /// Stopping the tag and waiting for IDLE (ticked); erases a held log on
    /// the way.
    Stopping,
    /// The tag's firmware is erasing its log (SRESET); ticked, with sector
    /// progress.
    Erasing,
    /// probe-rs is erasing, programming and verifying on a worker; no tick.
    /// Cannot be cancelled.
    Programming,
    /// Retest: probe-rs is reading the flash back on a worker; no tick.
    Reading,
    /// Reattaching to the tag after programming or a read-back (ticked).
    Connecting,
    /// The tag did not answer: waiting for the user to replug the base and
    /// press Retry, or Give Up. No tick.
    PowerCycle,
    /// The self-tests are running (ticked).
    Testing,
    /// Finished; the session is kept for its message.
    Done,
}
```

The waiting steps are driven by a 1 s tick, a deadline kept in the session
(`tick_at`) and fired by `Station::fire_timers`, which re-arms it before
running the step ([station.rs:605](../../applications/tagprogram-rs/core/src/engine/station.rs#L605)):

```rust
        if self
            .session
            .as_ref()
            .and_then(|s| s.tick_at)
            .is_some_and(|t| t <= now)
        {
            if let Some(s) = self.session.as_mut() {
                s.tick_at = Some(now + Duration::from_secs(1));
            }
            self.session_tick();
        }
```

and dispatched by step, [session.rs:423](../../applications/tagprogram-rs/core/src/engine/session.rs#L423):

```rust
    pub(crate) fn session_tick(&mut self) {
        let Some(step) = self.session.as_ref().filter(|s| s.running).map(|s| s.step) else {
            return;
        };
        match step {
            Step::Stopping => self.tick_stopping(),
            Step::Erasing => self.tick_erasing(),
            Step::Connecting => self.tick_connecting(),
            Step::Testing => self.tick_testing(),
            _ => {}
        }
    }
```

Programming and Reading have no tick: they end when their worker's `Done`
arrives. PowerCycle waits for the user's Retry or Give Up. Each waiting step
counts ticks against a limit (`STOP_TICKS = 30`, `CONNECT_TICKS = 6`,
`TEST_TICKS = 120`, [session.rs:58](../../applications/tagprogram-rs/core/src/engine/session.rs#L58)).
`Station::finish` ends every session and writes the record row. The engine's
`next_deadline` is the minimum of the four timers,
[station.rs:572](../../applications/tagprogram-rs/core/src/engine/station.rs#L572):

```rust
    pub(crate) fn next_deadline(&self) -> Option<Instant> {
        [
            self.base_poll,
            self.status_poll,
            self.attach_retry.map(|(t, _)| t),
            self.session.as_ref().and_then(|s| s.tick_at),
        ]
        .into_iter()
        .flatten()
        .min()
    }
```

> **Rust:** An array of four `Option<Instant>`s; `.into_iter().flatten()`
> skips the `None`s and yields the `Instant`s; `.min()` gives the earliest,
> or `None` when no timer is armed. `is_some_and(|t| t <= now)` is "is
> `Some` and the predicate holds", one call instead of `t && *t <= now`.

Compared with Tag Monitor, where timers live in both the engine (`Monitor`)
and the tag thread (status polls), all of Tag Programmer's timers live in the
station, because the station is also the code that talks to the tag.

### Prompts: a typed queue, answered by `answer` with the id

Prompts are Tag Monitor's style: a typed `PromptKind` in the toolkit's
`Prompts` queue, the page shown the front one, every kind answered by one
command, `answer {id, button, index?, path?}`. A prompt raised while another
is up waits its turn.
[station.rs:173](../../applications/tagprogram-rs/core/src/engine/station.rs#L173)
(doc comments dropped):

```rust
pub(crate) enum PromptKind {
    Alert {
        title: String,
        text: String,
        style: &'static str,
    },
    AutoAttach,
    RecordFile {
        default_path: PathBuf,
        exists: bool,
    },
    ChooseBase {
        label: &'static str,
        bases: Vec<UsbDev>,
        current: usize,
    },
    ConfirmQuit,
}
```

`ChooseBase` keeps the bases themselves, in the order the list showed them,
so the answer's index names the base the operator saw even if one is
plugged or unplugged while the list is open. The page never sees them: the
view turns the front prompt into the JSON the page draws, building each
base's line of text there, and adds the prompt's `id`
([view.rs:289](../../applications/tagprogram-rs/core/src/engine/view.rs#L289),
`prompt_json`). Raising a prompt is one call,
[station.rs:551](../../applications/tagprogram-rs/core/src/engine/station.rs#L551):

```rust
    pub(crate) fn ask(&mut self, kind: PromptKind) {
        self.prompts.ask(kind);
    }
```

with `alert` and `alert_styled` beside it for the commonest kind. The
answer, [station.rs:714](../../applications/tagprogram-rs/core/src/engine/station.rs#L714):

```rust
    fn answer(&mut self, args: &Value) {
        let Some(answer) = self.prompts.answer(args) else {
            info!("Ignored an answer to a prompt that is no longer shown");
            return;
        };
        let ok = answer.ok();
        let (button, index, path) = (answer.button, answer.index, answer.path);
        match answer.kind {
            PromptKind::Alert { .. } => {}
            PromptKind::AutoAttach => {
                self.set_auto_attach(ok);
                self.begin_checks();
            }
            PromptKind::RecordFile { .. } => match (button.as_str(), path) {
                ("default", _) => {
                    let path = self.default_record_path();
                    self.open_or_create_record(&path);
                }
                ("open", Some(path)) => self.open_record(Path::new(&path)),
                ("create", Some(path)) => self.open_or_create_record(Path::new(&path)),
                _ => {}
            },
            PromptKind::ChooseBase { bases, .. } => {
                if ok && let Some(base) = index.and_then(|i| bases.get(i)) {
                    self.use_base(base.clone());
                }
            }
            PromptKind::ConfirmQuit => {
                if button == "quit" {
                    self.exit = true;
                }
            }
        }
    }
```

`Prompts::answer` takes the answer only if its `id` is the front prompt's,
removes that prompt and returns its kind with the button, index and path
parsed out of the JSON; anything else, including an answer with no `id`,
returns `None`, and the let-else logs it and returns. So a late answer (the
page still showing a base list when the engine has queued an alert) cannot
act on the wrong question. Before the move, an answer without an `id` was
taken as answering whatever prompt was outstanding; now it is dropped, and
the page always sends one. The buttons are per kind: `ok` and `cancel` for
the yes-or-no and list kinds, `default`, `open` and `create` for the record
file, `quit` for `confirmQuit`; any other button cancels. The test
`prompts_queue_and_stale_answers_are_dropped`
([station.rs:2120](../../applications/tagprogram-rs/core/src/engine/station.rs#L2120))
queues two alerts, answers the waiting one's id and checks that nothing was
taken, answers the front one and checks that the second is now in front,
then checks that an answer with no `id` is dropped.

> **Rust:** `let (button, index, path) = (answer.button, ...)` moves the
> three fields out of `answer` before `match answer.kind` moves the fourth;
> a struct may be taken apart field by field like this. `match
> (button.as_str(), path)` matches a tuple, so one arm can test the button
> and whether a path came with it at once. `if ok && let Some(base) = ...`
> is a *let chain* (Rust 2024): a condition and a pattern match in one `if`,
> the body running only when both hold.

### The shell, and quitting

`tagprogram-rs/app/src/main.rs` describes Tag Programmer to the shared shell
as Tag Monitor's does, with both `keep_awake` and `close_through_engine`
set, so a window close or Cmd-Q reaches the engine as `quit`. Its sink,
[main.rs:57](../../applications/tagprogram-rs/app/src/main.rs#L57):

```rust
        let engine = engine::spawn(config, move |event| match event {
            Event::State(state) => events.state(state),
            Event::Log { level, text } => events.log(level, &text),
            Event::Exit => events.exit(),
        });
```

Its `quit` asks only when quitting would do harm: while probe-rs is erasing,
programming and verifying a tag, an exit would leave the tag with a partial
image. [station.rs:638](../../applications/tagprogram-rs/core/src/engine/station.rs#L638):

```rust
    fn programming(&self) -> bool {
        self.session
            .as_ref()
            .is_some_and(|s| s.running && s.step == Step::Programming)
    }

    fn quit(&mut self) {
        if self.programming() {
            self.ask(PromptKind::ConfirmQuit);
        } else {
            self.exit = true;
        }
    }
```

(doc comments dropped). At any other step, a test, a read-back, waiting for
a tag, `quit` exits at once. The page shows `confirmQuit` (its title and
text are filled in by the view) with Keep Programming as the default; Quit
answers with the button `quit`, which sets `exit`, and Keep Programming
answers `cancel`. Like every answer, both carry the prompt's `id`, so a
stale one is dropped. The
choice stays with the user, so a stuck programming job cannot trap the
program. The test `quit_asks_only_while_programming` in `station.rs` checks
both paths. The page's capability set is the shared one, so its
`Kit.setTitle` call is allowed, though the title it sets never changes.

> **Rust:** `is_some_and(|s| ...)` on an `Option` is true only if there is
> a value and the closure says yes for it, the one-call form of
> `opt && pred(*opt)`. `as_ref()` first turns `&Option<SessionState>` into
> `Option<&SessionState>`, so the test borrows the session instead of
> moving it out of the station.

### The page

[tagprogram-rs/ui/app.js](../../applications/tagprogram-rs/ui/app.js) has the
same skeleton as Tag Monitor's (`send`, `draw`, `Window`, `showPrompt`,
wiring), and since the move onto the toolkit the same protocol: menus from
the `menus` query, every prompt answered with `answer`, and `ready` at
start-up. What is its own is the dialogs it starts itself, below.

**The menus come from the engine.** The `menus` query answers the menu bar
as data (`menus()` in `view.rs`), and the page turns each entry into a kit
menu item by its `id`,
[app.js:457](../../applications/tagprogram-rs/ui/app.js#L457):

```js
  function menuItem(e) {
    if (e.kind === "separator") return "-";
    const item = { label: e.label, tooltip: e.tooltip || undefined };
    switch (e.id) {
      case "user_guide": item.action = userGuide; break;
      case "about": item.action = about; break;
      case "auto_attach":
        item.checked = !!(state && state.autoAttach);
        item.action = () => send("set_auto_attach", { on: !(state && state.autoAttach) });
        break;
      default: item.action = () => send(e.id);
    }
    return item;
  }
```

Help's two entries open pages; Attach Automatically shows `state.autoAttach`
as its check mark and sends its opposite; every other entry (`list_bases`,
`choose_record`, `refresh_releases`) sends the command its `id` names. Each
menu's `items` is a function, so the check mark is read each time the menu
opens. Until the move these menus were written out in the page.

**Prompts answer with `answer` and the prompt's id.** A one-line helper,
[app.js:63](../../applications/tagprogram-rs/ui/app.js#L63):

```js
  const answer = (id, button, extra) => send("answer", Object.assign({ id, button }, extra || {}));
```

and the dispatcher, [app.js:221](../../applications/tagprogram-rs/ui/app.js#L221):

```js
  function showPrompt(p) {
    if (!p || p.id === shownPrompt) return;
    shownPrompt = p.id;
    if (p.kind === "autoAttach") return promptAutoAttach(p);
    if (p.kind === "recordFile") return promptRecordFile(p);
    if (p.kind === "chooseBase") return promptChooseBase(p);
    if (p.kind === "confirmQuit") return promptConfirmQuit(p);
    if (p.kind === "alert") {
      return Kit.alert(p.title, p.text, null, p.style).then(() => answer(p.id, "ok"));
    }
    answer(p.id, "cancel");
  }
```

Each `prompt*` function answers with the prompt's id and its button, for
example `promptChooseBase`
([app.js:284](../../applications/tagprogram-rs/ui/app.js#L284)):

```js
  async function promptChooseBase(p) {
    const index = await Kit.chooseFromList("Choose a Base", p.label, p.items, p.current);
    if (index === null) answer(p.id, "cancel");
    else answer(p.id, "ok", { index });
  }
```

> **JavaScript:** `Object.assign({ id, button }, extra || {})` copies
> `extra`'s fields into a new object; `{ id, button }` is an object literal
> with shorthand properties (`id` alone means `id: id`).

**Start-up asks for the menus, listens, then sends `ready`**, and opens a
preview dialog if the URL asks for one
([app.js:474](../../applications/tagprogram-rs/ui/app.js#L474)):

```js
  Promise.all([
    engine.query("menus").then((m) => {
      menus = m;
      Kit.menuBar(document.getElementById("menubar"),
        menus.menus.map((menu) => ({ title: menu.title, items: () => menu.entries.map(menuItem) })));
    }),
    engine.on("log", (e) => log.append(`${e.level} ${e.text}`)),
    engine.on("state", (s) => {
      state = s;
      Kit.setTitle(s.title);
      draw();
      showPrompt(s.prompt);
    }),
  ]).then(async () => {
    await send("set_log_level", { level: ui.level });
    await send("ready");
    if (params.get("dialog") === "program") programDialog(params.get("label") || "");
    if (params.get("dialog") === "conflict") labelAccepted("Program Tag", params.get("label") || "BitTag-017");
  });
```

`await send("ready")` resolves when Tauri has queued the command, not when
the engine has acted on it.

**Some dialogs are started by the page and ask queries first.** The
Program dialog is the richest piece of front-end code in the three apps.
`programDialog` ([app.js:380](../../applications/tagprogram-rs/ui/app.js#L380)):

```js
  async function programDialog(initialLabel) {
    if (busy) return;
    busy = true;
    try {
      const pv = await engine.query("program_preview");
      if (!pv.ok) {
        await Kit.alert("Program Tag", pv.reason, null, "warning");
        return;
      }
      const buttons = [
        { label: "Cancel", value: null, default: false, cancel: true, enter: true },
        { label: "Erase and Program", value: "program", default: true, enabled: false },
      ];
      let label = "";
      const onLabel = (l) => {
        label = l;
        if (buttons[1].node) buttons[1].node.disabled = l === "";
      };
      const body = html`<${ProgramBody} pv=${pv} initialLabel=${initialLabel || ""} onLabel=${onLabel}/>`;
      if ((await Kit.dialog({ title: "Program Tag", body, buttons })) !== "program") return;
      if (await labelAccepted("Program Tag", label)) send("program", { label });
    } finally {
      busy = false;
    }
  }
```

- `busy` blocks a second dialog while one is open (a double click on
  Program); `try ... finally` clears it on every exit, including the early
  `return`s.
- `program_preview` answers what programming will erase, lose and keep, or
  why it cannot program.
- Return presses **Cancel** (`enter: true` on Cancel), "so the destructive
  step always takes a click". Erase and Program is styled as the default but
  starts disabled.
- The body reports the trimmed label through `onLabel`, which enables or
  disables the button through the `.node` that `Kit.dialog` attached to its
  button object. The `if (buttons[1].node)` guard is there because the body
  is rendered before the button row exists, and its first `onLabel` call
  could come before it.
- After the dialog, `label_conflict` asks whether the record already holds
  the label for another tag; only then is `program {label}` sent.

The body is a component with its own state, the only component in the
three apps that calls `useState`. [app.js:341](../../applications/tagprogram-rs/ui/app.js#L341):

```js
  function ProgramBody({ pv, initialLabel, onLabel }) {
    const [label, setLabel] = useState(initialLabel);
    const [notes, setNotes] = useState("");
    const seq = useRef(0);
    const input = useRef(null);
    useEffect(() => { setTimeout(() => input.current.select(), 0); }, []);
    useEffect(() => {
      const trimmed = label.trim();
      onLabel(trimmed);
      const mine = ++seq.current;
      Promise.resolve(trimmed ? engine.query("label_notes", { label: trimmed }) : []).then((extra) => {
        if (mine === seq.current) setNotes(pv.notes.concat(extra || []).join("\n"));
      });
    }, [label]);
```

and its field, [app.js:359](../../applications/tagprogram-rs/ui/app.js#L359):

```js
        <div class="kit-form-value"><input ref=${input} class="kit-field" type="text" value=${label}
          placeholder="The label on the board, e.g. BitTag-017"
          onInput=${(ev) => setLabel(ev.currentTarget.value)}/></div>
```

Why `useState` here and nowhere else: the label belongs to the dialog, not to
the engine. While the user types, no engine state exists for it, so the
component holds it (like a `QLineEdit` in a modal dialog), and the input is a
*controlled* one (`value=${label}`, updated on every `input` event), unlike
Tag Monitor's engine-driven fields.

The second effect runs whenever `label` changes: it reports the label, then
asks the engine for notes about it ("already recorded on ...", and so on).
Queries resolve out of order if the user types quickly, so each request takes
a sequence number (`mine`) and its answer is used only if no newer request
has been made since. A `useRef` holds the counter because it must survive
renders without causing one.

> **JavaScript:** `const [label, setLabel] = useState(initialLabel)` is array
> destructuring of the pair `useState` returns. `++seq.current` increments
> the box's value and yields the new one. The effect's closure captures
> `mine`, its own number, which is how the late `.then` knows whether it is
> stale.

Two smaller page details. Choosing "Other ELF file..." in the firmware popup
opens a file picker; if the user cancels, the page calls `draw()`, and the
kit's `Select` snaps the popup back to the engine's selection
([app.js:422](../../applications/tagprogram-rs/ui/app.js#L422)). And
`keepSpaces` ([app.js:73](../../applications/tagprogram-rs/ui/app.js#L73))
replaces each double space in a firmware entry with a no-break space and a
space, because a closed `<select>` collapses runs of spaces and qtprogram's
texts align columns with two.

Tag Programmer is also the only page that uses `StatusGrid` (the eight
Station rows, built by `checkRows` with Stop, Erase and Choose... buttons on
their rows), `Disclosure` and a `LogPane` as a drawer, and the inline
`Session` line (step text, progress bar, Retry and Cancel) instead of
progress sheets.

## Tag Calibrator

### What it does

It streams a tag's magnetometer and accelerometer, collects samples while the
user turns the tag, fits a hard- and soft-iron calibration, shows the fit's
quality and the samples on a sphere, shows the resulting compass heading and
the tag's attitude in 3D, and writes the constants to the tag: qtcalibrate's
behaviour. The maths is the C++ calibration session
(`calibcore::CalibrationSession`), reached through `tagcore_c` and the
`calibsession` crate ([chapter 8](08-bridges-to-cpp-and-python.md)). Its
design is [tagcalibrate-rs/design/architecture.md](../../applications/tagcalibrate-rs/design/architecture.md),
its protocol [design/ui-engine-contract.md](../../applications/tagcalibrate-rs/design/ui-engine-contract.md).

### The core: Tag Monitor's two threads, over a C++ session

The engine has Tag Monitor's threads: an engine thread that owns all state in
one `Calibrator` and never touches the tag, and a tag thread that owns
`tagcore::Tag`, takes `TagCmd`s and sends `Report`s. There is no `model/`
directory: the model *is* `calibsession::Session`. As in Tag Monitor, the
plumbing around the two threads (the handle, the channel, the loop, the
logger) is not in this crate: it is the shared engine toolkit,
`tagapp-engine`, read in the next section. What is left is `spawn`,
[tagcalibrate-rs/core/src/engine/mod.rs:83](../../applications/tagcalibrate-rs/core/src/engine/mod.rs#L83):

```rust
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let (engine, poster, inbox) = tagapp_engine::channel(QUERY_TIMEOUT, sink);
    let (tag_tx, tag_rx) = mpsc::channel();
    std::thread::Builder::new()
        .name("tagcalibrate-tag".into())
        .spawn(move || tagthread::run(tag_rx, poster))
        .expect("tag thread");
    let calibrator = Calibrator::new(config, tag_tx).expect("calibration session");
    tagapp_engine::spawn("tagcalibrate-engine", calibrator, inbox);
    engine
}
```

Three handles come out of `channel`: `engine` for the shell, `poster` for the
tag thread to send its reports on, and `inbox`, the engine thread's
receiving end, which `tagapp_engine::spawn` consumes. The tag thread's own
inbox (`tag_tx`/`tag_rx`) is still a plain `mpsc` channel, since only this
application knows what a `TagCmd` is. `QUERY_TIMEOUT` is 10 s. `Engine`,
`Event`, `LOGGER` and `UiLogger` are re-exported from the toolkit
(`pub use tagapp_engine::{Engine, Event, LOGGER, UiLogger};`), so the
application's `main.rs` names them through `tagcalibrate_core::engine` as
before. Tag Monitor's `spawn` is the same, with a `cancel` flag shared with
its tag thread
([chapter 11](11-engine-and-model.md#the-engine-handle-and-spawn)).

As in Tag Monitor and Tag Programmer, the state is built on the
caller's thread and then moved into the engine thread, which the toolkit's
`EngineState: Send` bound requires. For the calibrator that is only
possible because the C++ session handle is declared movable between threads
([calibsession-rs/src/lib.rs:232](../../libraries/calibsession-rs/src/lib.rs#L232),
`unsafe impl Send for Session {}`; chapter 8 explains it). Building it first
also makes a failure to create the C++ session (an ABI mismatch, or out of
memory) a panic at start-up rather than a dead engine thread. There is no
`cancel` flag: nothing here is long enough to cancel.

`Calibrator` is handed to the loop through the toolkit's trait, whose
methods are the calibrator's own,
[calibrator.rs:741](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L741)
(doc comments dropped):

```rust
impl EngineState for Calibrator {
    type Msg = Report;

    fn command(&mut self, cmd: &str, args: &Value) {
        Calibrator::command(self, cmd, args);
    }
    // query, view_state, next_deadline, fire_timers and take_exit likewise
    fn message(&mut self, report: Report) {
        self.report(report);
    }
    fn take_republish(&mut self) -> bool {
        self.ready.take_republish()
    }
}
```

> **Rust:** `Calibrator::command(self, cmd, args)` inside
> `impl EngineState for Calibrator` calls the *inherent* method (the one in
> `impl Calibrator`), not the trait method of the same name, which would
> recurse. Writing the type's path picks the inherent one, like
> `Base::f()` qualifying a call in C++. Inherent methods take precedence in
> method-call syntax too, so `self.command(...)` would also find it, but the
> path makes the intent plain.

### The engine toolkit underneath

`host/libraries/tagapp-engine-rs/src/lib.rs` is one file, fully documented;
its loop is the one [chapter 11](11-engine-and-model.md#the-engine-loop)
reads as the loop Tag Monitor runs, with no application in it. Three Rust devices make that work, and none of
them appears elsewhere in the tree.

**A trait with an associated type and default methods.**
[lib.rs:92](../../libraries/tagapp-engine-rs/src/lib.rs#L92) (doc comments
dropped, default bodies shortened):

```rust
pub trait EngineState: Send + 'static {
    type Msg: Send + 'static;

    fn command(&mut self, cmd: &str, args: &Value);
    fn query(&mut self, name: &str, args: &Value) -> Value;
    fn view_state(&mut self) -> Value;

    fn message(&mut self, msg: Self::Msg) { let _ = msg; }
    fn next_deadline(&self) -> Option<Instant> { None }
    fn fire_timers(&mut self, now: Instant) { let _ = now; }
    fn take_republish(&mut self) -> bool { false }
    fn take_exit(&mut self) -> bool { false }
}
```

> **Rust:** `type Msg` is an *associated type*: each implementation names
> one (`Report` for the calibrator, `Done` for Tag Programmer, `()` for Tag
> View), and the trait's
> methods and the toolkit's generic code refer to it as `Self::Msg` or
> `S::Msg`. In C++ it is a nested typedef a template expects of its
> parameter (`typename S::Msg`), checked when the `impl` is written rather
> than when a template is instantiated. A method with a body is a *default*:
> an implementation may leave it out, like a virtual function with a base
> implementation, but resolved at compile time. Tag View writes only the
> three required methods and `take_republish`; Tag Monitor, Tag Programmer
> and the calibrator write all eight. `: Send + 'static` on the trait means only a state that can be moved
> into the engine thread can implement it.

The loop, `run_loop` ([lib.rs:278](../../libraries/tagapp-engine-rs/src/lib.rs#L278)),
is generic over `S: EngineState`, so it is compiled once per application with
the calls made directly, not through a vtable.

**A boxed closure that hides the message type.** The engine thread's channel
carries `Msg<M>`, either a UI request or one of the application's messages,
so its sender's type depends on the application. The shell must hold the
handle without knowing that type, so `Engine` keeps a closure instead of
the sender, [lib.rs:157](../../libraries/tagapp-engine-rs/src/lib.rs#L157)
and [lib.rs:165](../../libraries/tagapp-engine-rs/src/lib.rs#L165):

```rust
type SendRequest = Arc<dyn Fn(Request) -> bool + Send + Sync>;

#[derive(Clone)]
pub struct Engine {
    send: SendRequest,
    query_timeout: Duration,
}
```

and `channel` makes it, [lib.rs:246](../../libraries/tagapp-engine-rs/src/lib.rs#L246):

```rust
    let ui_tx = tx.clone();
    let engine = Engine {
        send: Arc::new(move |request| ui_tx.send(Msg::Ui(request)).is_ok()),
        query_timeout,
    };
```

> **Rust:** the closure captures a `Sender<Msg<M>>` for one particular `M`,
> but its type as stored, `dyn Fn(Request) -> bool`, mentions only
> `Request`. That is type erasure, as `std::function<bool(Request)>` erases
> what a lambda captured. So `Engine` is not generic: every application's
> handle is the same type, and `tagapp-shell` can store one without a type
> parameter. The cost is one indirect call per command, which is nothing
> beside a channel send.

**A hand-written `Clone`.** The tag thread's handle is the other half,
generic because the tag thread knows its message type,
[lib.rs:202](../../libraries/tagapp-engine-rs/src/lib.rs#L202):

```rust
pub struct Poster<M> {
    tx: Sender<Msg<M>>,
}

// Derived Clone would require `M: Clone`; a sender clones whatever it sends.
impl<M> Clone for Poster<M> {
    fn clone(&self) -> Self {
        Poster {
            tx: self.tx.clone(),
        }
    }
}
```

> **Rust:** `#[derive(Clone)]` on a generic struct writes
> `impl<M: Clone> Clone for Poster<M>`: it bounds every type parameter,
> whether or not a field needs it. A `Sender<T>` clones for any `T`, and
> `Report` is not `Clone`, so the derived impl would make `Poster<Report>`
> uncloneable for no reason. Writing the impl by hand with `impl<M>` and no
> bound is the standard fix.

The rest is the prompt queue and the `ready` guard, read with the
calibrator's prompts below, and `UiLogger`, which chapter 11 reads in
[The logger](11-engine-and-model.md#the-logger). The toolkit's own tests, at the end of
`lib.rs`, drive the loop with a small `Probe` state and no window: two
`ready`s, a posted message, an unknown query and a `quit`.

### 100 ms samples, 200 ms quality

The tag thread reads the tag's calibration log every 100 ms while streaming,
as qtcalibrate's sample timer did, and otherwise reads the status every second
only to keep the tag awake. [tagthread.rs:19](../../applications/tagcalibrate-rs/core/src/engine/tagthread.rs#L19):

```rust
/// How often the calibration log is read while streaming: qtcalibrate's
/// 100 ms sample timer.
pub(crate) const SAMPLE_POLL: Duration = Duration::from_millis(100);
```

and [tagthread.rs:34](../../applications/tagcalibrate-rs/core/src/engine/tagthread.rs#L34):

```rust
/// One calibration sample: the magnetometer reading, and the accelerometer's
/// when the tag sent one.
pub(crate) type Sample = ([f32; 3], Option<[f32; 3]>);
```

The poll, [tagthread.rs:146](../../applications/tagcalibrate-rs/core/src/engine/tagthread.rs#L146):

```rust
    fn poll(&mut self) {
        if !self.tag.is_attached() {
            self.next_poll = None;
            return;
        }
        if self.streaming {
            if let Ok(ackn) = self.tag.calibration_log()
                && let Some(ack::Payload::CalibrationLog(log)) = ackn.payload
            {
                let samples = log
                    .data
                    .iter()
                    .filter_map(|d| {
                        let m = d.mag.as_ref()?;
                        let a = d.accel.as_ref().map(|a| [a.ax, a.ay, a.az]);
                        Some(([m.mx, m.my, m.mz], a))
                    })
                    .collect();
                self.report(Report::Samples(samples));
            }
            self.next_poll = Some(Instant::now() + SAMPLE_POLL);
        } else {
            let _ = self.tag.status();
            self.next_poll = Some(Instant::now() + KEEP_AWAKE);
        }
    }
```

> **Rust:** `if let Ok(ackn) = ... && let Some(ack::Payload::CalibrationLog(log)) = ackn.payload`
> is a *let chain*: two pattern matches joined by `&&`, the second using a
> binding from the first. It reads "if the call succeeded and its payload is
> the calibration-log variant of the protobuf `oneof`" (prost generates the
> `oneof` as a Rust enum, [chapter 5](05-protobuf-and-the-tag-api.md)).
> Inside `filter_map`, `d.mag.as_ref()?` returns `None` from the closure for
> a sample with no magnetometer reading, which drops it.

Each read becomes one `Report::Samples(Vec<Sample>)` carrying however many
samples the tag's log held. On the engine thread, `report` gives each read a
batch index and passes every sample to `sample`
([calibrator.rs:519](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L519)):

```rust
    fn sample(&mut self, batch_index: i32, sample_index: i32, sample: Sample) {
        let (mag, accel) = sample;
        if let Some(a) = accel
            && let Some(orientation) = self.session.orientation(mag, a)
        {
            let gravity = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt();
            self.live = Some(Live {
                orientation,
                gravity,
            });
        }
        if self.collecting {
            if self.capture.active {
                self.capture.samples.push(CaptureSample {
                    batch_index,
                    sample_index,
                    sample,
                });
            }
            if self.session.add_sample(mag, accel) {
                debug!("new calibration, B={:.2} uT", self.session.field());
            }
            self.newest = Some(if self.session.has_calibration() {
                self.session.calibrate(mag)
            } else {
                mag
            });
        }
    }
```

Every sample with an accelerometer reading updates the live compass; while
collecting, the sample is also captured (qtcalibrate's capture file) and fed
to the C++ solver, which keeps up to `calibsession::SLOTS` = 650 samples
([calibsession-rs/src/lib.rs:68](../../libraries/calibsession-rs/src/lib.rs#L68)).

The quality figures are recomputed on their own timer, every 200 ms while
collecting from a tag. [calibrator.rs:24](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L24):

```rust
/// How often the quality figures are recomputed while collecting from a tag:
/// qtcalibrate's 200 ms quality timer, one update every two samples.
const QUALITY_PERIOD: Duration = Duration::from_millis(200);

/// In a replay, the quality update runs every this many samples instead, so
/// the result depends on the capture alone (qtcalibrate's
/// `kReplayQualityInterval`).
const REPLAY_QUALITY_INTERVAL: usize = 2;
```

and the timers, [calibrator.rs:617](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L617):

```rust
    pub fn fire_timers(&mut self, now: Instant) {
        if self.quit_due.is_some_and(|d| now >= d) {
            warn!("The tag did not detach in time; quitting anyway");
            self.quit_due = None;
            self.exit = true;
        }
        if self.replay_due.is_some_and(|d| now >= d) {
            self.replay_due = Some(now + self.config.replay_period);
            self.replay_tick();
        }
        if self.quality_due.is_some_and(|d| now >= d) {
            self.quality_due = Some(now + QUALITY_PERIOD);
            self.quality_update();
        }
    }
```

`quality_update` logs qtcalibrate's four `DEBUG` lines (`magquality:`,
`residual:`, `gravity:`, `retention:`) in its exact format, which is what the
replay test compares against qtcalibrate's own log.

**Replay is the mock tag.** With `TAGCALIBRATE_REPLAY` set (or
`EngineConfig::replay` in a test), the engine needs no tag: it loads a capture
file and feeds one sample per `replay_period` from its own timer
(`replay_tick` above), with a quality update every second sample so the result
depends on the capture alone, not on timing. That is how
`core/tests/replay_log.rs` runs the real engine and the real C++ maths at a
1 ms period and checks the last quality lines against qtcalibrate's.

### The whole sphere in every state

The view state carries the data the 3D views draw.
[view.rs:195](../../applications/tagcalibrate-rs/core/src/engine/view.rs#L195):

```rust
    fn sphere_view(&self) -> Value {
        let points: Vec<f32> = self.session.points().into_iter().flatten().collect();
        json!({
            "radius": self.session.field(),
            "points": points,
            "newest": self.newest,
        })
    }
```

`points()` returns up to 650 `[f32; 3]`; flattening gives `[x, y, z, x, y, z,
...]` in microtesla, which the page hands straight to three.js. The
orientation part sends a heading in degrees (declination and battery direction
applied), preformatted readouts, and the tag model's rotation as a quaternion,
[view.rs:224](../../applications/tagcalibrate-rs/core/src/engine/view.rs#L224):

```rust
        // qtcalibrate turns its tag model by the raw angles, yaw
        // counter-clockwise.
        let model = model_rotation(a.pitch as f32, a.roll as f32, (360.0 - a.yaw) as f32);
```

`model_rotation` returns `[w, x, y, z]`, scalar first, as Qt's `QQuaternion`
does. Remember that order for the page.

This is the heaviest use of "send the whole state": while streaming, every
100 ms read and every 200 ms quality update rebuilds the state, sphere
included, compares it with the last, and emits it if it changed, which during
collection is nearly every time. Up to about 15 states a second, each with up
to 1950 floats, work without trouble in practice; there is no delta protocol.

### Prompts, ready and quit

Prompts are Tag Monitor's style: a typed `PromptKind` (`Alert`, `ChooseBase`,
`SaveCapture`) in a queue, the page shown the front one, answered by
`answer {id, button, index?, path?}`. The queue is the toolkit's
`Prompts<PromptKind>`; a prompt raised while another is up waits its turn.
[calibrator.rs:425](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L425):

```rust
    fn answer(&mut self, args: &Value) {
        let Some(answer) = self.prompts.answer(args) else {
            return;
        };
        let ok = answer.ok();
        match answer.kind {
            PromptKind::Alert { .. } => {}
            PromptKind::ChooseBase(bases) => {
                match answer.index.and_then(|i| bases.get(i)).filter(|_| ok) {
                    Some((base, _)) => self.send(TagCmd::Attach(base.clone())),
                    None => self.attaching = false,
                }
            }
            // SaveCapture: save to answer.path when ok
        }
    }
```

`Prompts::answer` ([tagapp-engine lib.rs:431](../../libraries/tagapp-engine-rs/src/lib.rs#L431))
takes the answer only if its `id` is the front prompt's; then it removes that
prompt and returns its kind with the button, index and path already parsed
out of the JSON (`button` defaults to `cancel`, an empty path is `None`).
Any other id, a stale answer to a prompt the page no longer shows, returns
`None` and leaves the queue alone, which the let-else turns into an early
return. Because `answer.kind` is moved out, `ChooseBase(bases)` binds the
list of bases by value; no clone of the prompt is needed.

`ready` starts the replay or attaches, but only the first time,
[calibrator.rs:245](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L245):

```rust
    fn ready(&mut self) {
        if !self.ready.ready() {
            return;
        }
        if let Some(path) = self.config.replay.clone() {
            self.start_replay(&path);
        } else if self.config.auto_attach {
            self.attach();
        }
    }
```

`self.ready` is the toolkit's `Ready`, the same guard Tag Monitor's `ready`
uses ([chapter 11](11-engine-and-model.md#the-engine-loop)): `ready()` sets
the republish flag every time and returns true only the first time, and the
calibrator's `take_republish` returns `Ready::take_republish`, so the loop
sends a reloaded page the whole state at once. A reload therefore neither
re-attaches nor restarts a replay; it redraws what the engine has. Tag
View's `ready` has the same guard around opening its startup log.

Quit works as in Tag Monitor (the shared shell turns a window close, and on
macOS the menu bar's Quit item, into `quit` and exits on `Event::Exit`, see
[chapter 10](10-tauri-shell.md#the-macos-application-menu); the engine
detaches first and exits once the tag thread reports `Detached`, or after
`QUIT_WAIT`, 5 s), less the
unsaved-changes question, since there is no document. The reason given here
is the tag's own: "exiting at once left the tag calibrating, and a
calibrating tag refuses everything but Stop" ([calibrator.rs:226](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L226)).

### The page

[tagcalibrate-rs/ui/app.js](../../applications/tagcalibrate-rs/ui/app.js) is
Tag Monitor's page with different contents: `menus` from a query, `menuItem`
with page-side ids (`stream`, `declination`, whose label shows the current
value, `battery_forward` and the log items), `answer {id, ...}` prompts
(`alert`, `chooseBase`, `saveFile`), tabs kept mounted, `ready` at start-up,
and only the Cmd/Ctrl+Q shortcut. Its scripts add three files to the kit's,
[index.html:30](../../applications/tagcalibrate-rs/ui/index.html#L30):

```html
  <script src="../../../ui-kit/preact-htm.js"></script>
  <script src="../../../ui-kit/kit.js"></script>
  <script src="../../../ui-kit/kit-components.js"></script>
  <script src="../../../ui-kit/heading-dial.js"></script>
  <script src="three.min.js"></script>
  <script src="views.js"></script>
  <script src="mock-capture.js"></script>
  <script src="mock-engine.js"></script>
  <script src="app.js"></script>
```

The page keeps one value the engine never sees, `ui.direction`, the 3D tag
view's camera azimuth (Screen Direction). Its number fields use Tag Monitor's
focus rule packaged as a component,
[app.js:245](../../applications/tagcalibrate-rs/ui/app.js#L245):

```js
  function NumberField({ value, decimals, step, min, max, tooltip, onValue }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
      if (document.activeElement !== ref.current) ref.current.value = Number(value).toFixed(decimals);
    });
    return html`<input ref=${ref} class="kit-field number" type="number" step=${step} min=${min} max=${max}
      title=${tooltip} onChange=${(ev) => {
        const v = parseFloat(ev.currentTarget.value);
        if (!Number.isNaN(v)) onValue(v);
      }}/>`;
  }
```

Declination is sent to the engine (`set_declination`); Screen Direction is
stored in `ui.direction` and redrawn. The popup menu is attached to the whole
`.pages` area but suppressed over the log pane and over the sphere's canvas.

**Library: three.js.** A WebGL scene graph for the browser: a `Scene` of
`Mesh`es (a geometry plus a material), `Group`s, lights and a camera, drawn by
a `WebGLRenderer` onto a `<canvas>`. It plays the part Qt 3D and
`MyAttitude.qml` played in qtcalibrate. The page vendors **r159**
([three.min.js](../../applications/tagcalibrate-rs/ui/three.min.js)) and
defines the global `THREE`. Its header gives the reason for the pin: "r159 is
the last release with this classic (non-module) build, which is what lets the
page open from file:// with no build step". Later releases are ES modules
only, which the `file://` rule of [chapter 12](12-ui-kit-and-pages.md#classic-scripts-loaded-in-a-fixed-order)
excludes.

[views.js](../../applications/tagcalibrate-rs/ui/views.js) wraps three.js in
Preact components, so the page uses `<${SphereView} sphere=${s.sphere}/>`
like any other component. Both views share a helper that sets up a renderer,
[views.js:83](../../applications/tagcalibrate-rs/ui/views.js#L83):

```js
  function stage(canvas, camera, fit) {
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
    renderer.setPixelRatio(window.devicePixelRatio || 1);
    const scene = new THREE.Scene();
    scene.add(new THREE.HemisphereLight(0xffffff, 0x777777, 1.7));
    const sun = new THREE.DirectionalLight(0xffffff, 1.3);
    sun.position.set(200, -300, 400);
    scene.add(sun);
    const render = () => renderer.render(scene, camera);
    const observer = new ResizeObserver(() => {
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (!w || !h) return;
      renderer.setSize(w, h, false);
      fit(camera, w, h);
      camera.updateProjectionMatrix();
      render();
    });
    observer.observe(canvas);
    return { scene, render, dispose() { observer.disconnect(); renderer.dispose(); } };
  }
```

- **Render on demand.** There is no animation loop. A frame is drawn when
  something changed: new data from the engine, a drag, a zoom, a resize. An
  idle window costs no GPU time, as a `QOpenGLWidget` that calls `update()`
  only on change.
- **ResizeObserver** is a browser callback on an element's size change. It
  re-sizes the renderer and redraws. A view on a hidden tab has size zero, so
  the callback returns early; when the tab is shown, the size changes and the
  view draws correctly. That is why both views can stay mounted on hidden
  tabs.
- `alpha: true` leaves the canvas background transparent, so the CSS
  `--view-bg` token (white, or dark with the theme) shows through.

The tag view shows the pattern for every three.js component.
[views.js:318](../../applications/tagcalibrate-rs/ui/views.js#L318):

```js
  function TagView({ quaternion, batteryForward, direction }) {
    const canvas = useRef(null);
    const view = useRef(null);
    // Built in a layout effect, declared first, so it exists before the
    // effects that update it run.
    useLayoutEffect(() => {
      const camera = new THREE.PerspectiveCamera(60, 1, 1, 5000);
      const st = stage(canvas.current, camera, (c, w, h) => { c.aspect = w / h; });
      const grid = new THREE.GridHelper(1000, 20, 0xbbbbbb, 0xdddddd);
      grid.rotation.x = Math.PI / 2;
      st.scene.add(grid);
      for (const [x, y, z, color] of AXES) st.scene.add(axis([x * 400, y * 400, z * 400], color));
      const tag = new THREE.Group();
      tag.add(new THREE.Mesh(new THREE.BoxGeometry(100, 200, 10),
        new THREE.MeshStandardMaterial({ color: 0x1f8f2f, roughness: 0.6 })));
      const battery = new THREE.Mesh(new THREE.CylinderGeometry(55, 55, 30, 48),
        new THREE.MeshPhongMaterial({ color: 0xd8dadd, shininess: 60 }));
      battery.rotation.x = Math.PI / 2;
      tag.add(battery);
      st.scene.add(tag);
      const cam = orbit(canvas.current, camera, Math.hypot(500, 40), 0, Math.atan2(40, 500) / deg, st.render);
      view.current = { st, tag, battery, cam };
      return () => { cam.dispose(); st.dispose(); };
    }, []);
    useLayoutEffect(() => { view.current.cam.set(direction, null); }, [direction]);
    useLayoutEffect(() => {
      const v = view.current;
      const [w, x, y, z] = quaternion;
      v.tag.quaternion.set(x, y, z, w);
      v.battery.position.set(0, batteryForward ? 100 : -100, 20);
      v.st.render();
    }, [quaternion, batteryForward]);
    return html`<canvas ref=${canvas} class="view tag-view"
      title="The tag's attitude. Drag to look from another side."></canvas>`;
  }
```

- Preact renders only the `<canvas>`. The scene is built once, at mount
  (`deps` `[]`), kept in `view.current` (a `useRef`, so a member variable),
  and its returned function disposes the renderer and the drag listeners at
  unmount. Effects run in the order they are declared, so the builder comes
  first and the effects that update the scene can rely on `view.current`.
- The other effects update the scene when their props change, then render
  once.
- **The quaternion order swap.** The engine sends `[w, x, y, z]`
  (`QQuaternion`'s order); three.js's `Quaternion.set` takes `(x, y, z, w)`.
  `const [w, x, y, z] = quaternion;` unpacks it by position and the next
  line passes it in three.js's order. Read this line whenever the model turns
  the wrong way.
- `orbit` ([views.js:38](../../applications/tagcalibrate-rs/ui/views.js#L38))
  is a camera that circles the origin with Z up, driven by pointer drags
  (`setPointerCapture` keeps the drag going outside the canvas) and by
  `direction`.

Because each `state` event is freshly parsed JSON, `quaternion` and `sphere`
are new arrays and objects every time, so their effects run (and the view
renders) on every state, whether or not the numbers changed. `direction` and
`batteryForward` are a number and a boolean and compare by value.

`SphereView` ([views.js:167](../../applications/tagcalibrate-rs/ui/views.js#L167))
reproduces qtcalibrate's `magPlot` the same way: an orthographic camera at a
fixed scale (so the circle's size on screen *is* the field magnitude), a
see-through disc drawn with a small GLSL shader, the samples as
`THREE.Points` with per-vertex colours (dark near, pale far), the newest in
yellow, and drag-to-turn and wheel zoom. Its update effect turns the sphere so
the newest sample faces the viewer, [views.js:284](../../applications/tagcalibrate-rs/ui/views.js#L284):

```js
      v.geometry.setAttribute("position", new THREE.Float32BufferAttribute(sphere.points, 3));
      v.geometry.setAttribute("color", new THREE.Float32BufferAttribute(new Float32Array(sphere.points.length), 3));
      v.geometry.computeBoundingSphere();
      const newest = sphere.newest;
      v.latest.visible = !!newest;
      if (newest) {
        v.latest.position.fromArray(newest);
        const p = v.latest.position.clone().normalize();
        if (p.lengthSq() > 0) v.focus.setFromUnitVectors(p, new THREE.Vector3(0, 0, -1));
```

The flat `points` array from Rust goes into a GPU buffer as it is, three
floats per vertex. `setFromUnitVectors` is the shortest rotation taking one
direction to another, three.js's `QQuaternion::rotationTo`. The page
computes nothing about the tag: points, heading and quaternion all come from
the engine (views.js's header says so).

The Orientation page's compass is the kit's SVG `HeadingDial`
([chapter 12](12-ui-kit-and-pages.md#heading-dialjs)), driven by
`state.orientation.heading`.

### A mock that replays a real capture

Tag Calibrator's mock engine is the only one that does not invent its data.
`mock-capture.js` is one line of generated JavaScript, `window.CAPTURE =
{...}`, holding the reference qtcalibrate capture
(`host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json`)
joined with the angles qtcalibrate itself computed for each sample. It is made
by [tools/make_mock_capture.py](../../applications/tagcalibrate-rs/tools/make_mock_capture.py),
whose docstring explains how to get those angles out of a patched qtcalibrate.
The mock replays it at the capture's 10 Hz,
[mock-engine.js:253](../../applications/tagcalibrate-rs/ui/mock-engine.js#L253):

```js
    /** Runs or stops the replay, at the capture's 10 Hz. */
    function replay(on) {
      clearInterval(timer);
      timer = on ? setInterval(step, 100) : null;
    }
```

and applies the capture's final fit throughout, so the browser preview shows
the app's own results without reimplementing them. Its header
([mock-engine.js:11](../../applications/tagcalibrate-rs/ui/mock-engine.js#L11))
also writes out the view state, commands and queries, and its scenarios are
`startup`, `idle`, `streaming` (the default), `collecting`, `collected`
(with `?percent=`), `orientation`, `prompt-base` and `alert`; `?sample=N` and
`?pose=heading,pitch,roll` fix the display. Like Tag Monitor's mock, it is
loaded in the shipped app but never constructed there, so the 63 KB capture is
embedded in the binary unused.

## The three side by side

| | Tag Programmer | Tag Monitor | Tag Calibrator |
| --- | --- | --- | --- |
| Ported from | qtprogram | qtmonitor | qtcalibrate |
| Created | first ([0035](../../../docs/decisions/0035-host-tagprogram-rs.md)) | second ([0037](../../../docs/decisions/0037-host-tagmonitor-rs.md)) | third ([0039](../../../docs/decisions/0039-host-tagcalibrate-rs.md)) |
| Engine state type | `Station` (with `session.rs`, `view.rs`) | `Monitor` (with `model/` rules) | `Calibrator` over `calibsession::Session` |
| Who owns `tagcore::Tag` | the engine thread | a tag thread | a tag thread |
| Other threads | short-lived workers via `spawn_work` | none | none |
| `Msg` variants | the toolkit's: a UI request, or `Done` (a closure from a worker) as `EngineState::Msg`, posted through a `Poster` | the toolkit's: a UI request, or `Report` as `EngineState::Msg`, posted through a `Poster` | the toolkit's, as Tag Monitor |
| Shared between threads, besides channels and the logger's sink | `Arc<Catalog>` with internal `Mutex`es | `Arc<AtomicBool>` download cancel | none |
| Timers | base poll 1 s, status poll 400 ms, attach retry, session tick 1 s, all in the station | engine: a tick and the quit deadline; tag thread: 400 ms status poll | tag thread: 100 ms samples or 1 s keep-awake; engine: 200 ms quality, replay tick, quit wait |
| Long work | probe-rs programming and read-back, GitHub listing and downloads | SQLite download on the tag thread, with cancel | none |
| C++ | none | `tagcore_c` SQLite writer (feature `sqlitelog`) | `tagcore_c` calibration session, always |
| First page command | `ready` (begins the checks, or asks the first-run question, the first time, through the toolkit's `Ready`; republishes every time) | `ready` (attaches the first time, through the toolkit's `Ready`; republishes every time) | `ready` (attaches or starts the replay the first time, through the toolkit's `Ready`; republishes every time) |
| Prompts | typed `PromptKind`s in the toolkit's `Prompts` queue | typed `PromptKind`s in the toolkit's `Prompts` queue | typed `PromptKind`s in the toolkit's `Prompts` queue |
| Prompt answers | `answer {id, button, ...}` | `answer {id, button, ...}` | `answer {id, button, ...}` |
| Menus | `menus` query (table in `view.rs`) | `menus` query (table in `model/menus.rs`) | `menus` query (table in `view.rs`) |
| Window close and Cmd-Q | through the engine: `quit` exits at once, or asks (`confirmQuit`) while a tag is being programmed | through the engine: `quit`, `Event::Exit` after the detach (at most 5 s) | as Tag Monitor, with no unsaved-changes question |
| Shell (`tagapp-shell`) | `keep_awake`, `close_through_engine`; build rpath `Never` | the same flags; rpath `WithFeature("sqlitelog")` | the same flags; rpath `Always` |
| Page-initiated queries | `program_preview`, `label_notes`, `label_conflict`, `retest_default_label` | none beyond `menus`, `about`, `user_guide_url` | none beyond `menus`, `about`, `user_guide_url` |
| Progress display | inline `Session` row | `Kit.dialog` sheets following `erase`/`download` | quality figures and the sphere |
| Kit pieces only it uses | `StatusGrid`, `Disclosure`, log drawer, `useState` | generated config form | `HeadingDial`, three.js views |
| Mock data | hand-written, scenario chosen at `create` | hand-written plus the real BITTAG form, scenario at `ready` | a real capture replayed at 10 Hz, scenario at `ready` |
| Headless tests | 30 unit tests in `model/`, `view.rs` and `station.rs`; `examples/smoke.rs` lists the base | Qt oracle files and model tests ([chapter 11](11-engine-and-model.md)) | replay of the reference capture through the real engine and C++ maths |
| Window | 900 x 700 | 1100 x 640 | 1040 x 800 |

Read as a history, the table shows the pattern settling: Tag Programmer
proved the engine/page split with a design shaped by qtprogram (workers,
page-side menus, one command per answer); Tag Monitor moved the menus and the
prompt protocol into the engine and added the tag thread; Tag Calibrator took
Tag Monitor's shape unchanged and spent its novelty on the data path and the
graphics. The last step took that shape out of the applications into
`tagapp-engine`, which all three (and Tag View) now run on. Tag Programmer
took the toolkit's protocol (`ready`, the prompt queue and `answer`, menus
from the engine) but kept its threading, so the rows that still differ are
the ones about threads: who owns the `Tag`, the workers, and what the
application's message is.

## Try it

- Open `host/applications/tagcalibrate-rs/ui/index.html` in a browser and
  watch the mock replay the real capture; try
  `?scenario=collected&percent=100`, `?scenario=orientation&page=orientation`
  and `?pose=90,10,-20&page=orientation`. Drag both 3D views.
- Open `host/applications/tagprogram-rs/ui/index.html?dialog=program&label=BitTag-017`
  and type in the Label field: the Erase and Program button follows the
  field, and Return presses Cancel.
- `rg -n "spawn_work\(" host/applications/tagprogram-rs/core/src` lists every
  piece of work that leaves the engine thread; read each `apply` closure and
  check what it re-checks.
- `cargo doc -p tagprogram-core --document-private-items --open` (from
  `host/`, with `CARGO_TARGET_DIR` on the build disk) renders `Station`,
  `SessionState` and `Step` with their documentation.
- `cargo test -p tagprogram-core` runs its model, view and station tests;
  none needs hardware (the `station.rs` tests open a libusb context but
  send nothing on USB). Do not run its examples on the bench without reading their
  headers: `smoke` talks to a base on USB, and `bench` programs a real tag.
- With a CMake build of `tagcore_c`,
  `TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo test -p tagcalibrate-core`
  replays the reference capture through the real engine and compares its
  quality lines with qtcalibrate's (see
  [Checking a change](../../applications/tagcalibrate-rs/design/architecture.md#checking-a-change)).
- Compare the three `EngineState` impls: `rg -n "impl EngineState for"
  host/applications` finds Tag Programmer's `Station`, Tag Monitor's
  `Monitor`, Tag Calibrator's `Calibrator` and Tag View's `Viewer`; `run_loop` in
  `host/libraries/tagapp-engine-rs/src/lib.rs`, about 25 lines, runs all of
  them. `cargo test -p tagapp-engine` runs the toolkit's own tests, with no
  window and no tag.

## Where next

- [Chapter 13](13-tracing-start.md) traces one Tag Monitor command end to end.
- [Chapter 7](07-programming-tags-and-bases.md) reads `tagprobe::program`
  and probe-rs, the other side of Tag Programmer's worker.
- [Chapter 8](08-bridges-to-cpp-and-python.md) reads `calibsession` and
  `tagcore_c`, the other side of Tag Calibrator's `Session`.
- [tagprogram-rs/design/engine.md](../../applications/tagprogram-rs/design/engine.md)
  lists what runs on the engine thread and what on workers, and the hazards
  the session avoids.
