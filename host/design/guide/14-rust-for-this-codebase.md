---
type: design
status: current
summary: Chapter 14 of the Host Software Guide - the Rust the host code relies on, explained for an engineer who knows C++ (ownership and borrowing as the replacement for the C++ mutexes, Result and Option, enums with data and match, traits, threads and channels, Send and Sync, Drop, closures, features and build scripts, the generated protobuf types, the FFI rules, the documentation lints), each with where it appears in this tree.
---

# 14. Rust in this code base

This chapter is for a reader who knows C++ and is new to Rust. It is not a
Rust tutorial; [The Rust Programming Language](https://doc.rust-lang.org/book/)
is, and is worth two evenings. It is the subset the host code depends on,
with the C++ idea each piece replaces and the place in this tree to see it.

## Ownership is the design

In C++ this code base used a recursive mutex shared by three classes to
serialise use of a USB device. In Rust the compiler does that job, and
reading the signatures tells you the rules:

| Signature | Means | Here |
| --- | --- | --- |
| `fn attach(&mut self, ...)` | Exclusive access: no other reference to this value exists while the call runs. One caller at a time, checked at compile time | `LinkAdapt`, `TagMonitor`: every method that touches the probe |
| `fn status(&self) -> ...` | Shared access; the value is not changed through this reference (unless it holds a `Mutex`, `Cell` or atomic inside) | `Tag`: callable from several threads because it locks inside |
| `fn new() -> Self`, `Tag { inner: Mutex<Inner> }` | Owned by value: `Tag` contains its `TagMonitor`, which contains its `LinkAdapt`. Dropping the outer drops the inner. No pointers, no `shared_ptr` | the three layers of `tagcore` |
| `Arc<Tag>` | Shared ownership across threads (an atomic refcount, like `shared_ptr`) when two threads really need one value | a `Tag` shared by a poll and a command path |
| `Box<Config>` | Heap allocation, like `unique_ptr`; used to keep a large message out of an enum's size | `TagCmd::Start(Box<Config>)` |

The borrow checker refuses a program in which two mutable references to one
value could exist at once, or a reference could outlive what it points to.
Nearly every "why won't this compile" in this tree is one of those two, and
the answer is usually to restructure who owns what, not to add a lock. The
applications are the worked example: one thread owns all the state and
other threads send it messages, so there is nothing to lock (chapter 10).

## Errors: `Result` and `Option`

There are no exceptions. A function that can fail returns
`Result<T, E>`: either `Ok(value)` or `Err(error)`. A value that may be
absent is `Option<T>`: `Some(value)` or `None`. The `?` operator returns the
error early, like a `try` that cannot be forgotten:

```rust
let info = tag.tag_info()?;        // Result<TagInfo, TagError>: on Err, return it from this function
let serial = base.serial.as_deref().unwrap_or("");   // Option: a default when None
match tag.status() {
    Ok(status) => ...,
    Err(e) if e.transport_failed => ...,   // the request may not have arrived
    Err(e) => ...,                          // the firmware refused
}
```

This tree's convention, from `tagcore`: the layer the caller uses returns
`Result` with a typed error (`TagError`, `WriterError`, `SessionError`);
lower layers return `Option` or `bool` and log the reason, because the C++
only logged it. A `Result` the caller ignores is a compiler warning, which is
how "we forgot to check" was removed from the tools.

## Enums carry data, and `match` must be complete

A Rust `enum` is a tagged union: each variant may carry fields. The code
uses this everywhere a C++ class hierarchy or a `switch` on a kind field
would be:

```rust
pub(crate) enum TagCmd { FindBases, Attach(UsbDev), Start(Box<Config>), Download(PathBuf), ... }
pub enum Progress { Step(&'static str), Bytes { done: u64, total: u64 } }
enum PromptKind { Alert { title: String, text: String, style: &'static str }, ChooseBase(Vec<...>), SaveCapture }
```

`match` on an enum must handle every variant, so adding a `TagCmd` makes
every `match` that forgets it fail to compile. That is the tool used to keep
the tag thread, the engine and the view in step.

The protobuf enums are the exception you will meet first: prost generates
`TagState` as a Rust enum, but a message field holding one (`Status.state`)
is an `i32`, because a message from a newer firmware may carry a value this
build does not know. The generated accessor `status.state()` converts,
falling back to the default; `TagState::try_from(i32)` converts and tells
you if it could not. `Req` and `Ack` payloads are `oneof` fields and arrive
as an `Option<req::Payload>` enum, matched by variant.

## Traits: interfaces without inheritance

A trait is a set of methods a type implements; there is no class
inheritance. The ones this tree relies on:

| Trait | Means | Here |
| --- | --- | --- |
| `Send` | The value may be moved to another thread | Automatic for most types; `LogWriter` and the sessions are `Send` because the C handle may move |
| `Sync` | The value may be shared by reference between threads | Deliberately **not** implemented by the C-bridge wrappers (`Send + !Sync`), which is the C side's one-thread rule as a type |
| `Drop` | A destructor | `Tag` detaches, `LogWriter` closes its handle, `Session` frees it: RAII as in C++ |
| `Default`, `Clone`, `Debug`, `PartialEq` | What `#[derive(...)]` writes for you: a default value, a copy, a printable form, equality | On nearly every data type; `PartialEq` is what lets the engine compare view states |
| `log::Log` | A logger; the global `log` facade calls it | `UiLogger` in each application |
| `serde::Serialize` | Conversion to JSON through `serde_json` | View-state pieces, settings |
| probe-rs's `DebugProbe` | What a probe must provide | `FieldSafeProbe` in `tagprobe` |

`impl Trait for Type` blocks, and `impl Type` blocks of inherent methods,
may be anywhere in the crate: `TagMonitor`'s methods are in five files.

## Threads and channels

The applications' concurrency is `std::thread::spawn` and
`std::sync::mpsc::channel` (multi-producer, single-consumer): a thread owns
its state and receives messages; senders are cloned to whoever needs to
send. `recv_timeout` is the engine loop's wait with a deadline. A closure
sent through a channel (`Msg::Done(Box<dyn FnOnce(&mut Station) + Send>)`)
is how a worker's result is applied on the engine thread: the work happens
elsewhere, the mutation happens where the state lives. `Arc<AtomicBool>` is
the cancel flag a download checks between pages. There are no condition
variables and no raw locks in the applications; `Mutex` appears in `Tag` and
in the logger's sink.

## Closures and iterators

`|x| expr` is a lambda; it captures by reference unless `move` is written,
and the borrow checker applies to captures too. Iterator chains
(`.iter().map(..).filter(..).collect()`) are used as in modern C++ ranges,
and are the idiom for building lists for the view state.

## Modules, crates, features, build scripts

- A **crate** is a compilation unit and a package; a **module** is a file or
  a `mod` block; `pub` makes an item visible outside its module,
  `pub(crate)` inside the crate only. Chapter 2 has the layout.
- `use crate::model::commands::Command;` imports by path from the crate
  root; `use super::Msg;` from the parent module.
- A **feature** is a compile-time switch (`#[cfg(feature = "sqlitelog")]`),
  declared in `Cargo.toml` and enabled with `--features`; chapter 13 lists
  them.
- A **build script** (`build.rs`) runs before the crate compiles and may
  generate code into `OUT_DIR` or emit link directives: `tagcore`'s runs
  protoc, `tagcore-c-sys`'s finds `libtagcore_c`, the `app` crates' stage the
  page for Tauri.
- `#[derive(Parser)]` (clap) and `#[tauri::command]` are **procedural
  macros**: code generated from an annotated item. `html\`...\`` in the
  pages is htm, not Rust.

## Foreign functions

The bridge crates are the only `unsafe` in the tree. `extern "C" { fn
tlw_open(...) -> *mut TlwWriter; }` declares a C function; calling it is
`unsafe` because the compiler cannot check what C does with the pointers.
The rules the wrappers keep (chapter 8) are what make each call sound: a
handle owned exactly once and freed in `Drop`; buffers that outlive the
call; returned strings copied before the next call; `#[repr(C)]` on every
struct that crosses, so its layout is C's. Every `unsafe` block states which
rule applies. Add no `unsafe` outside those crates.

## Reading the generated code

`tagcore::proto` is generated at build time and lives in
`target/.../out/_.rs`; `cargo doc -p tagcore` is the comfortable way to read
it. Field names are the `.proto` names in snake case; nested messages are
modules (`ack::Payload`, `config::Interval`); enums have `as_str_name()` for
the protobuf name, which the tools print.

## The documentation lints

Every crate has:

```toml
[lints.rust]
missing_docs = "warn"
[lints.clippy]
missing_docs_in_private_items = "warn"
```

so every item, private ones included, needs a `///` comment, and
`cargo clippy` reports the ones that lack it. The first `//!` block in each
file is its summary. Read code top-down through its comments; they were
written to carry the design, not restate the signatures.
## Habits to copy

- Start a new file with a `//!` block: what it holds, what it follows, its
  threading and hardware side effects.
- Return `Result` from anything that can fail; log at the layer that knows
  the reason; never `unwrap()` on something a user's input or a tag's answer
  can make fail (an `expect("...")` on a programming invariant is fine).
- Prefer `&mut self` and one owner to a lock; prefer a message to a shared
  variable.
- Make illegal states unrepresentable: an enum for a state machine, a
  `Box<Config>` carried by the one command that needs it, `Option` for what
  may be absent.
- Run `cargo clippy` and `cargo doc` with warnings denied before calling a
  change done.
