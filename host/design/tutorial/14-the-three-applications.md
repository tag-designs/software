---
type: design
status: current
summary: "Tag Programmer and Tag Calibrator read against Tag Monitor - what each does, its core crate's types and threads, its page, and where it follows or departs from the pattern - ending with a side-by-side table of the three."
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

Tag Programmer was ported from qtprogram first. Tag Monitor followed, and its
shell says of itself "The shell is Tag Programmer's ... with those two
changes" (closing through the engine, and a settings file). Tag Calibrator
copied Tag Monitor's shell and engine shape. So when the two older apps
differ, Tag Monitor's way is the later one, and Tag Calibrator usually agrees
with it. The differences are listed in the table at the end; the sections
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
  w -- "Msg::Done(closure)" --> eng
  eng -- "state / log events" --> page
```

The message type says it.
[tagprogram-rs/core/src/engine/mod.rs:134](../../applications/tagprogram-rs/core/src/engine/mod.rs#L134):

```rust
/// Messages to the engine thread, applied in the order they arrive.
pub(crate) enum Msg {
    /// A UI command: its name and its JSON arguments (see
    /// [`Station::command`]).
    Command(String, Value),
    /// A UI query: its name, its JSON arguments and the channel for its
    /// answer (see [`Station::query`]).
    Query(String, Value, Sender<Value>),
    /// A worker finished, or reports progress; its closure is applied to the
    /// station on the engine thread. The station also posts one to itself to
    /// defer opening the record until after the first view is published
    /// (`Station::begin_checks`).
    Done(Box<dyn FnOnce(&mut Station) + Send>),
}
```

> **Rust:** `Box<dyn FnOnce(&mut Station) + Send>` is a heap-allocated,
> type-erased callable that takes `&mut Station`, may be called once, and
> may be sent to another thread: roughly a move-only
> `std::function<void(Station&)>`. Sending *code* rather than data is how a
> worker affects state it cannot touch: the `Station` lives on the engine
> thread and no one else holds a reference to it. The nearest Qt idiom is
> `QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)`. The enum
> has no `#[derive(Debug)]`, unlike Tag Monitor's, because a boxed closure
> cannot be printed.

The loop is Tag Monitor's without the republish and exit steps.
[mod.rs:295](../../applications/tagprogram-rs/core/src/engine/mod.rs#L295):

```rust
fn run(config: EngineConfig, sink: Sink, tx: Sender<Msg>, rx: Receiver<Msg>) {
    let mut station = Station::new(config, tx);
    let mut last_state = Value::Null;
    loop {
        let wait = station
            .next_deadline()
            .map(|d| d.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match rx.recv_timeout(wait) {
            Ok(Msg::Command(cmd, args)) => station.command(&cmd, &args),
            Ok(Msg::Query(name, args, reply)) => {
                let _ = reply.send(station.query(&name, &args));
            }
            Ok(Msg::Done(apply)) => apply(&mut station),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break,
        }
        station.fire_timers(Instant::now());
        let state = station.view_state();
        if state != last_state {
            sink(Event::State(state.clone()));
            last_state = state;
        }
    }
}
```

`Station::new` runs on the engine thread itself, and the station is given a
clone of the channel's sender (`tx`), which is how it starts workers that can
reply. The worker launcher, [station.rs:426](../../applications/tagprogram-rs/core/src/engine/station.rs#L426)
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
            let done: Box<dyn FnOnce(&mut Station) + Send> =
                match std::panic::catch_unwind(std::panic::AssertUnwindSafe(work)) {
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
            let _ = tx.send(Msg::Done(done));
        });
    }
```

`work` runs on the new thread and produces a `T`; `apply` is packaged with
that `T` into a `Done` closure and runs later on the engine thread.

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
[model/catalog.rs:291](../../applications/tagprogram-rs/core/src/model/catalog.rs#L291):

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
[station.rs:1308](../../applications/tagprogram-rs/core/src/engine/station.rs#L1308):

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
what is happening. [station.rs:730](../../applications/tagprogram-rs/core/src/engine/station.rs#L730):

```rust
        let _ = self.tx.send(Msg::Done(Box::new(move |s: &mut Station| {
            let mut record = RecordLog::new();
            match record.open(Path::new(&path)) {
                Ok(()) => s.record = Some(record),
                Err(e) => warn!("{e}"),
            }
            s.check_record();
            if s.record.is_none() {
                s.raise_record_prompt();
            }
        })));
```

### The Station

`Station` ([station.rs:188](../../applications/tagprogram-rs/core/src/engine/station.rs#L188))
is qtprogram's `Station` plus the parts of its `MainWindow` that held state.
Its `impl` is spread over three files: `station.rs` (checks, base, attach,
polling, firmware choice, record, prompts), `session.rs` (the programming
session) and `view.rs` (the view state).

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
| `prompt: Option<Prompt>` | at most one outstanding question |
| `catalog: Arc<Catalog>` | the release list, shared with workers |

`command` ([station.rs:568](../../applications/tagprogram-rs/core/src/engine/station.rs#L568))
is one `match` on the command name, with two small closures to read
arguments ([station.rs:569](../../applications/tagprogram-rs/core/src/engine/station.rs#L569)):

```rust
        let s = |k: &str| args.get(k).and_then(Value::as_str).unwrap_or("").to_owned();
        let b = |k: &str| args.get(k).and_then(Value::as_bool).unwrap_or(false);
```

A missing or mistyped argument reads as `""` or `false`, and an unknown
command is logged as a warning: nothing is returned to the page, which matches
the shell's `command` returning `()` ([chapter 12](12-ui-kit-and-pages.md#kitcreateengine-tauri-or-the-mock)).

`start` versus `ready`: the page's first command is `start`, not `ready`.
[station.rs:673](../../applications/tagprogram-rs/core/src/engine/station.rs#L673):

```rust
    fn begin(&mut self) {
        if self.started {
            return;
        }
        self.started = true;
        if !self.settings.contains(key::AUTO_ATTACH) {
            self.raise(json!({ "kind": "autoAttach" }));
            return;
        }
        self.begin_checks();
    }
```

It is one-shot: a second `start` is ignored, and nothing forces the engine to
send the whole state again. Tag Monitor's `ready` does both jobs (start once,
republish every time), so a reloaded Tag Monitor page gets its state at once;
a reloaded Tag Programmer page waits for the next change. In the app the page
loads once, so the difference shows only in development.

### Programming: tagprobe on a worker, progress as closures

A Program session stops the tag through its monitor, has the tag's own
firmware erase a held log, then detaches and programs with probe-rs on a
worker. [session.rs:725](../../applications/tagprogram-rs/core/src/engine/session.rs#L725),
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
                    let _ = tx.send(Msg::Done(Box::new(move |s: &mut Station| {
                        s.program_progress(p)
                    })));
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
([tagprobe-rs/src/lib.rs:423](../../libraries/tagprobe-rs/src/lib.rs#L423)):
`Step(&'static str)` ("Erasing", "Programming", "Verifying", "Reading back")
or `Bytes { done, total }`. Each event becomes its own `Done` closure, so the
station is updated on its own thread. [session.rs:762](../../applications/tagprogram-rs/core/src/engine/session.rs#L762):

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
[view.rs:203](../../applications/tagprogram-rs/core/src/engine/view.rs#L203):

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
worker, a `Msg::Done` closure, `SessionState.progress` on the engine thread,
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
running the step ([station.rs:529](../../applications/tagprogram-rs/core/src/engine/station.rs#L529)):

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

and dispatched by step, [session.rs:425](../../applications/tagprogram-rs/core/src/engine/session.rs#L425):

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
`TEST_TICKS = 120`, [session.rs:60](../../applications/tagprogram-rs/core/src/engine/session.rs#L60)).
`Station::finish` ends every session and writes the record row. The engine's
`next_deadline` is the minimum of the four timers,
[station.rs:496](../../applications/tagprogram-rs/core/src/engine/station.rs#L496):

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

### Prompts: one at a time, answered by per-kind commands

Tag Monitor keeps a queue of typed prompts and answers every kind with one
`answer {id, button, ...}` command. Tag Programmer keeps **one** prompt,
whose body is free-form JSON, and a new one replaces the old.
[station.rs:480](../../applications/tagprogram-rs/core/src/engine/station.rs#L480):

```rust
    /// Raises a prompt, replacing any outstanding one, with a fresh id.
    pub(crate) fn raise(&mut self, body: Value) {
        self.prompt_seq += 1;
        self.prompt = Some(Prompt {
            id: self.prompt_seq,
            body,
        });
    }

    /// Raises a warning-style `alert` prompt: a message box with OK.
    pub(crate) fn alert(&mut self, title: &str, text: &str) {
        self.raise(json!({ "kind": "alert", "title": title, "text": text, "style": "warning" }));
    }
```

The view adds the id to the body ([view.rs:279](../../applications/tagprogram-rs/core/src/engine/view.rs#L279)),
and the page shows each id once, as Tag Monitor's does. But the answers are
**distinct commands** that carry no id: `answer_auto_attach {on}`,
`choose_base {index}`, `record_use_default`, `record_open {path}`,
`record_create {path}` and `prompt_cancel`, each of which clears whatever
prompt is outstanding. [station.rs:594](../../applications/tagprogram-rs/core/src/engine/station.rs#L594):

```rust
            "answer_auto_attach" => {
                self.prompt = None;
                self.set_auto_attach(b("on"));
                self.begin_checks();
            }
            "list_bases" => self.list_bases(),
            "choose_base" => {
                self.prompt = None;
                let index = args.get("index").and_then(Value::as_u64).unwrap_or(0) as usize;
                self.choose_base(index);
            }
```

So in Tag Programmer an answer always applies to the current prompt, whatever
its id; Tag Monitor's id check is what later made late answers harmless.

### The shell

`tagprogram-rs/app/src/main.rs` is the shell Tag Monitor's was copied from,
less two things. It does not intercept the window's close (the window simply
closes, and `Event` has no `Exit` variant), and its capability file does not
grant `core:window:allow-set-title`, so the page's `Kit.setTitle` call to the
native window is refused silently (harmless: the title it sets does not
change). The sink, [main.rs:145](../../applications/tagprogram-rs/app/src/main.rs#L145):

```rust
            let engine = engine::spawn(config, move |event| {
                let _ = match event {
                    Event::State(state) => handle.emit("state", state),
                    Event::Log { level, text } => {
                        handle.emit("log", json!({ "level": level, "text": text }))
                    }
                };
            });
```

Both arms return `Result<(), tauri::Error>`, discarded together by `let _ =`.

### The page

[tagprogram-rs/ui/app.js](../../applications/tagprogram-rs/ui/app.js) has the
same skeleton as Tag Monitor's (`send`, `draw`, `Window`, `showPrompt`,
wiring), with four differences.

**The menus are written in the page**, not fetched with a `menus` query.
[app.js:419](../../applications/tagprogram-rs/ui/app.js#L419):

```js
  Kit.menuBar(document.getElementById("menubar"), [
    { title: "Help", items: [
      { label: "Tag Programmer User Guide", tooltip: "Open the Tag Programmer page of the user manual", action: userGuide },
      { label: "About Tag Programmer", action: about },
    ] },
    { title: "Station", items: () => [
      { label: "Attach Automatically", checked: state && state.autoAttach,
        action: () => send("set_auto_attach", { on: !(state && state.autoAttach) }) },
      { label: "Choose Base...", action: () => send("list_bases") },
      { label: "Choose Record File...", action: () => send("choose_record") },
      { label: "Refresh Firmware Releases", action: () => send("refresh_releases") },
    ] },
  ]);
```

The Station menu's `items` is a function, so its check mark follows
`state.autoAttach` each time it opens.

**Prompts answer with their own commands.** [app.js:207](../../applications/tagprogram-rs/ui/app.js#L207):

```js
  function showPrompt(p) {
    if (!p || p.id === shownPrompt) return;
    shownPrompt = p.id;
    if (p.kind === "autoAttach") return promptAutoAttach();
    if (p.kind === "recordFile") return promptRecordFile(p);
    if (p.kind === "chooseBase") return promptChooseBase(p);
    if (p.kind === "alert") {
      return Kit.alert(p.title, p.text, null, p.style).then(() => send("prompt_cancel"));
    }
    send("prompt_cancel");
  }
```

**Start-up sends `start`**, then opens a preview dialog if the URL asks for
one ([app.js:435](../../applications/tagprogram-rs/ui/app.js#L435)):

```js
  Promise.all([
    engine.on("log", (e) => log.append(`${e.level} ${e.text}`)),
    engine.on("state", (s) => {
      state = s;
      Kit.setTitle(s.title);
      draw();
      showPrompt(s.prompt);
    }),
  ]).then(async () => {
    await send("set_log_level", { level: ui.level });
    await send("start");
    if (params.get("dialog") === "program") programDialog(params.get("label") || "");
    if (params.get("dialog") === "conflict") labelAccepted("Program Tag", params.get("label") || "BitTag-017");
  });
```

`await send("start")` resolves when Tauri has queued the command, not when
the engine has acted on it.

**Some dialogs are started by the page and ask queries first.** The
Program dialog is the richest piece of front-end code in the three apps.
`programDialog` ([app.js:348](../../applications/tagprogram-rs/ui/app.js#L348)):

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
three apps that calls `useState`. [app.js:309](../../applications/tagprogram-rs/ui/app.js#L309):

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

and its field, [app.js:327](../../applications/tagprogram-rs/ui/app.js#L327):

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
([app.js:390](../../applications/tagprogram-rs/ui/app.js#L390)). And
`keepSpaces` ([app.js:61](../../applications/tagprogram-rs/ui/app.js#L61))
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
directory: the model *is* `calibsession::Session`. `spawn`,
[tagcalibrate-rs/core/src/engine/mod.rs:228](../../applications/tagcalibrate-rs/core/src/engine/mod.rs#L228):

```rust
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let sink: Sink = Arc::new(sink);
    *SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    set_log_level("INFO");
    let (tx, rx) = mpsc::channel();
    let (tag_tx, tag_rx) = mpsc::channel();
    {
        let tx = tx.clone();
        std::thread::Builder::new()
            .name("tagcalibrate-tag".into())
            .spawn(move || tagthread::run(tag_rx, tx))
            .expect("tag thread");
    }
    let calibrator = Calibrator::new(config, tag_tx).expect("calibration session");
    std::thread::Builder::new()
        .name("tagcalibrate-engine".into())
        .spawn(move || run(calibrator, sink, rx))
        .expect("engine thread");
    Engine { tx }
}
```

Unlike the other two, the state is built on the caller's thread and then
moved into the engine thread. That is only possible because the C++ session
handle is declared movable between threads
([calibsession-rs/src/lib.rs:232](../../libraries/calibsession-rs/src/lib.rs#L232),
`unsafe impl Send for Session {}`; chapter 8 explains it). Building it first
also makes a failure to create the C++ session (an ABI mismatch, or out of
memory) a panic at start-up rather than a dead engine thread. There is no
`cancel` flag: nothing here is long enough to cancel.

> **Rust:** The `{ let tx = tx.clone(); ... }` block clones the sender into
> a new `tx` that shadows the outer one only inside the block, so the
> closure can `move` the clone and the outer `tx` stays for the `Engine`.

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
([calibrator.rs:530](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L530)):

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
collecting from a tag. [calibrator.rs:23](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L23):

```rust
/// How often the quality figures are recomputed while collecting from a tag:
/// qtcalibrate's 200 ms quality timer, one update every two samples.
const QUALITY_PERIOD: Duration = Duration::from_millis(200);

/// In a replay, the quality update runs every this many samples instead, so
/// the result depends on the capture alone (qtcalibrate's
/// `kReplayQualityInterval`).
const REPLAY_QUALITY_INTERVAL: usize = 2;
```

and the timers, [calibrator.rs:628](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L628):

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
`SaveCapture`), one at a time in an `Option<Prompt>` (a new one replaces the
old), answered by `answer {id, button, index?, path?}`.
[calibrator.rs:432](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L432):

```rust
    fn answer(&mut self, args: &Value) {
        let Some(prompt) = self.prompt.take_if(|p| Some(p.id) == args["id"].as_u64()) else {
            return;
        };
```

> **Rust:** `Option::take_if(pred)` moves the value out of the option,
> leaving `None`, only if the predicate holds; otherwise it leaves it in
> place and returns `None`. With let-else, an answer for any other id is
> ignored in one line.

`ready` starts the replay or attaches, [calibrator.rs:256](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L256):

```rust
    fn ready(&mut self) {
        if let Some(path) = self.config.replay.clone() {
            self.start_replay(&path);
        } else if self.config.auto_attach {
            self.attach();
        }
    }
```

Note the difference from Tag Monitor: there is no "first time only" guard and
no forced republish. A reloaded page's `ready` does not reset an attached
tag, because `attach` ([calibrator.rs:284](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L284))
returns at once while attached or attaching; it attaches again only if the
user detached in the meantime. In a replay, each `ready` reloads the capture
and starts it over. Either way the reloaded page is not sent the state it
missed, and waits for the next change to get one. In the app the page sends
`ready` once.

Quit works as in Tag Monitor (the shell turns a window close into `quit` and
exits on `Event::Exit`; the engine detaches first and exits once the tag
thread reports `Detached`, or after `QUIT_WAIT`, 5 s), less the
unsaved-changes question, since there is no document. The reason given here
is the tag's own: "exiting at once left the tag calibrating, and a
calibrating tag refuses everything but Stop" ([calibrator.rs:237](../../applications/tagcalibrate-rs/core/src/engine/calibrator.rs#L237)).

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
| `Msg` variants | `Command`, `Query`, `Done(closure)` | `Command`, `Query`, `Tag(Report)` | `Command`, `Query`, `Tag(Report)` |
| Shared between threads, besides channels and the logger's sink | `Arc<Catalog>` with internal `Mutex`es | `Arc<AtomicBool>` download cancel | none |
| Timers | base poll 1 s, status poll 400 ms, attach retry, session tick 1 s, all in the station | engine: a tick and the quit deadline; tag thread: 400 ms status poll | tag thread: 100 ms samples or 1 s keep-awake; engine: 200 ms quality, replay tick, quit wait |
| Long work | probe-rs programming and read-back, GitHub listing and downloads | SQLite download on the tag thread, with cancel | none |
| C++ | none | `tagcore_c` SQLite writer (feature `sqlitelog`) | `tagcore_c` calibration session, always |
| First page command | `start` (once; no republish) | `ready` (attaches the first time; republishes every time) | `ready` (attaches unless already attached, or restarts the replay; no republish) |
| Prompts | one `Option<Prompt>`, JSON body, replaced by the next | typed `PromptKind`s in a queue | typed `PromptKind`, one `Option<Prompt>`, replaced by the next |
| Prompt answers | per-kind commands, no id | `answer {id, button, ...}` | `answer {id, button, ...}` |
| Menus | written in the page | `menus` query (table in `model/menus.rs`) | `menus` query (table in `view.rs`) |
| Window close | not intercepted; no `Event::Exit` | intercepted, `quit`, `Event::Exit` after the detach (at most 5 s) | as Tag Monitor, with no unsaved-changes question |
| `allow-set-title` capability | no | yes | yes |
| Page-initiated queries | `program_preview`, `label_notes`, `label_conflict`, `retest_default_label` | none beyond `menus`, `about`, `user_guide_url` | none beyond `menus`, `about`, `user_guide_url` |
| Progress display | inline `Session` row | `Kit.dialog` sheets following `erase`/`download` | quality figures and the sphere |
| Kit pieces only it uses | `StatusGrid`, `Disclosure`, log drawer, `useState` | generated config form | `HeadingDial`, three.js views |
| Mock data | hand-written, scenario chosen at `create` | hand-written plus the real BITTAG form, scenario at `ready` | a real capture replayed at 10 Hz, scenario at `ready` |
| Headless tests | 28 unit tests in `model/` and `view.rs`; `examples/smoke.rs` lists the base | Qt oracle files and model tests ([chapter 11](11-engine-and-model.md)) | replay of the reference capture through the real engine and C++ maths |
| Window | 900 x 700 | 1100 x 640 | 1040 x 800 |

Read as a history, the table shows the pattern settling: Tag Programmer
proved the engine/page split with a design shaped by qtprogram (workers,
page-side menus, one command per answer); Tag Monitor moved the menus and the
prompt protocol into the engine and added the tag thread; Tag Calibrator took
Tag Monitor's shape unchanged and spent its novelty on the data path and the
graphics.

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
- `cargo test -p tagprogram-core` runs its model and view tests; none needs
  hardware. Do not run its examples on the bench without reading their
  headers: `smoke` talks to a base on USB, and `bench` programs a real tag.
- With a CMake build of `tagcore_c`,
  `TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo test -p tagcalibrate-core`
  replays the reference capture through the real engine and compares its
  quality lines with qtcalibrate's (see
  [Checking a change](../../applications/tagcalibrate-rs/design/architecture.md#checking-a-change)).
- Compare the three loops directly: the `fn run` in each
  `core/src/engine/mod.rs` is about 25 lines.

## Where next

- [Chapter 13](13-tracing-start.md) traces one Tag Monitor command end to end.
- [Chapter 7](07-programming-tags-and-bases.md) reads `tagprobe::program`
  and probe-rs, the other side of Tag Programmer's worker.
- [Chapter 8](08-bridges-to-cpp-and-python.md) reads `calibsession` and
  `tagcore_c`, the other side of Tag Calibrator's `Session`.
- [tagprogram-rs/design/engine.md](../../applications/tagprogram-rs/design/engine.md)
  lists what runs on the engine thread and what on workers, and the hazards
  the session avoids.
