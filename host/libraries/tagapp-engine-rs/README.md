---
type: readme
status: current
summary: "tagapp-engine, the engine toolkit the Tauri applications' core crates share -- the Engine handle, the engine loop over an application's EngineState, the Poster a tag thread reports through, the prompt queue, the ready guard and the UI logger -- which applications use it, what an application supplies, and how to build and check it."
---

# tagapp-engine (Rust)

The engine half of the application pattern, as a library: the plumbing every
application's `core` crate had repeated. It has no Tauri dependency, so a
core crate that uses it still builds and tests without a window; the shell,
[`tagapp-shell`](../tagapp-shell-rs/README.md), wraps its `Engine` for the
page.

Tag Monitor, Tag Calibrator and Tag View run on it. Tag Programmer still
has its own copy of the same plumbing and moves onto it when it is next
changed ([the proposal](../../design/proposals/unified-app-shell.md),
phase 4).

An application supplies only its state, by implementing `EngineState`; the
toolkit supplies the rest:

- the `Engine` handle the shell holds: `command` (fire-and-forget) and
  `query` (waits for the answer, up to a timeout the application sets);
- one engine thread that owns the state and applies every message in the
  order it arrives: the UI's commands and queries, and the application's
  own messages (a tag thread's reports) posted through a `Poster`;
- the loop: wait until the next message or the application's next timer
  deadline, apply the message, fire the timers, rebuild the view state and
  emit it only if it changed (or a `ready` asked for it), then emit `Exit`
  if the application asked to exit;
- the `ready` guard: the first `ready` starts the engine (attach, open the
  startup file), and every `ready` resends the whole view state, so a
  reloaded page is redrawn at once and never attaches or opens anything
  again;
- the prompt queue: a prompt raised while another is up waits its turn
  instead of replacing it, each has a fresh id, and an answer to any id but
  the front prompt's is ignored;
- the logger: the `log` crate's records, the engine's and those of the
  libraries it calls, forwarded to the page's log pane at the level the UI
  chose.

## Use

```rust
use std::time::Duration;
use serde_json::{Value, json};
use tagapp_engine::{EngineState, Event, Ready};

struct Counter {
    total: i64,
    ready: Ready,
}

impl EngineState for Counter {
    type Msg = (); // a tag thread's report type, or () without one
    fn command(&mut self, cmd: &str, args: &Value) {
        match cmd {
            "ready" => {
                if self.ready.ready() {
                    // first time only: attach, open the startup file
                }
            }
            "add" => self.total += args["n"].as_i64().unwrap_or(0),
            other => log::warn!("Unknown command {other}"),
        }
    }
    fn query(&mut self, name: &str, _args: &Value) -> Value {
        tagapp_engine::query_error(&format!("unknown query {name}"))
    }
    fn view_state(&mut self) -> Value {
        json!({ "total": self.total })
    }
    fn take_republish(&mut self) -> bool {
        self.ready.take_republish()
    }
}

let _ = log::set_logger(&tagapp_engine::LOGGER);
let (engine, poster, inbox) = tagapp_engine::channel(Duration::from_secs(10), |event| {
    // forward Event::State, Event::Log and Event::Exit to the shell
});
// hand `poster` to the tag thread, if there is one, then:
tagapp_engine::spawn("counter-engine", Counter { total: 0, ready: Ready::default() }, inbox);
```

| Item | What it is |
| --- | --- |
| `EngineState` | The trait the application's state implements. Required: `command`, `query`, `view_state`. Optional, with defaults that do nothing: `message` (one `Msg`), `next_deadline` and `fire_timers`, `take_republish` (return `Ready::take_republish`), `take_exit` |
| `channel(query_timeout, sink)` | Makes the `Engine`, the `Poster<Msg>` and the `Inbox`. Installs `sink` as the logger's destination at once, so a tag thread started before `spawn` already logs to the page, and resets the level to `INFO` |
| `spawn(name, state, inbox)` | Starts the engine thread, named `name`, running the loop until every `Engine` and `Poster` is dropped. Panics only if a thread cannot be started |
| `Engine` | `command(cmd, args)` and `query(name, args)`; `query` returns `Value::Null` if the engine thread has gone or does not answer within the timeout. Cloneable, and the same type for every application |
| `Poster<M>` | `post(msg)`, from any thread; false once the engine thread has gone. Cloneable |
| `Event` | `State(Value)`, `Log {level, text}`, `Exit` |
| `Ready` | `ready()` (true the first time only), `started()`, `take_republish()` |
| `Prompts<K>` | `ask(kind) -> id`, `front()`, `is_empty()`, `answer(args) -> Option<Answer<K>>` (`None` for a missing or stale id), `remove_if(pred)` |
| `Answer<K>` | The answered prompt's `kind`, its `button` (`cancel` when none was given), `index`, `path` (`None` for an empty one), and `ok()` |
| `UiLogger`, `LOGGER`, `set_log_level(name)` | The logger to install with `log::set_logger`, and the level the page's menu sets (`TRACE`..`ERROR`; anything else means `INFO`) |
| `query_error(text)` | `{ "error": text }`, for a query that cannot be answered |

The sink is called on the engine thread for `State` and `Exit`, and on
whatever thread logged for `Log`, so it must be cheap and must never call
`Engine::query`.

| Application | `Msg` | Query timeout | Timers | Prompts | Exit |
| --- | --- | --- | --- | --- | --- |
| Tag Monitor | the tag thread's `Report` | 10 s | 1 s silence tick, quit deadline | `Prompts<PromptKind>` | after the detach on `quit` (at most 5 s) |
| Tag Calibrator | the tag thread's `Report` | 10 s | quality, replay tick, quit wait | `Prompts<PromptKind>` | after the detach on `quit` |
| Tag View | `()` | 30 s (a large log can take that long to open ahead of a query) | none | none | never: its window closes without asking |

## Requirements on an application

- **One engine per process.** The logger's sink and level are process-wide;
  `channel` replaces the sink.
- **Install `LOGGER` once**, with `log::set_logger`, before or after
  `channel`; records logged before `channel` are dropped.
- **Guard `ready` with `Ready`** and return its `take_republish` from
  `EngineState::take_republish`, or a reloaded page waits for the next
  change to be drawn.
- **`fire_timers` checks its own deadlines.** It is called after every
  message, not only when a deadline passes.
- **An application whose shell closes through the engine**
  (`close_through_engine` in `tagapp-shell`) must handle `quit` and
  eventually return true from `take_exit`, or the window cannot be closed.

## Building and checking

The crate builds with the host workspace. Its tests cover the prompt queue
(ids, stale answers, `remove_if`), the `ready` guard, and the loop (a change
emitted once, a second `ready` resending the same state, a posted message,
an unknown query, and `Exit`), with no window and no tag. The applications'
own tests run through it: Tag Calibrator's replay of the reference capture
and Tag View's engine tests. Tag Monitor's unit tests drive its `Monitor`
directly, without the loop.

```sh
cd host
cargo test -p tagapp-engine
cargo clippy -p tagapp-engine --all-targets
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items -p tagapp-engine
```

The pattern it implements is chapter 10 of the
[Host Software Guide](../../design/guide/10-application-pattern.md#the-engine-toolkit).
Why there is one toolkit, and the order the applications move onto it:
[the proposal](../../design/proposals/unified-app-shell.md).
