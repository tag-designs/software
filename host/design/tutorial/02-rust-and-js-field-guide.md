---
type: design
status: current
summary: "A reference to every Rust and JavaScript construct the host tools use that a C++ programmer may not know, each explained once with its nearest C++ or Qt equivalent, one real example from the tree, and links to the chapters that discuss it; with an alphabetical index."
---

# 2. A field guide to the Rust (and JavaScript) in this code

This chapter is a reference, not a walk. When a construct in the code, or
in a **Rust:** or **JavaScript:** aside in another chapter, stops you, look
it up here: each entry says what the construct means, the nearest C++ (or
Qt) equivalent, shows one real instance from the tree with its file and
line, and links to the chapter sections that explain it where it matters.
The [index](#index) at the end is alphabetical.

The large ideas are in [chapter 14 of the Host Software
Guide](../guide/14-rust-for-this-codebase.md) and are not repeated here:
[ownership as the replacement for locks](../guide/14-rust-for-this-codebase.md#ownership-is-the-design),
[`Result` and `Option`](../guide/14-rust-for-this-codebase.md#errors-result-and-option),
[enums with data and exhaustive `match`](../guide/14-rust-for-this-codebase.md#enums-carry-data-and-match-must-be-complete),
[traits](../guide/14-rust-for-this-codebase.md#traits-interfaces-without-inheritance)
and [threads and channels](../guide/14-rust-for-this-codebase.md#threads-and-channels).
Read that chapter once; use this one when a detail gets in the way.

Only constructs that occur in this code are listed. Every crate uses
**edition 2024**, which needs Rust 1.88 or later (let chains are the
reason); a construct from an older edition behaves the same unless an
entry says otherwise. Line numbers drift as the code changes; the names
in the examples are the stable reference.

## Reading a signature

### References: `&T`, `&mut T`, `&self`, `&mut self`

`&T` is a shared borrow (read-only, many may exist), `&mut T` an exclusive
one (exactly one, and no `&T` alongside it). The compiler checks both rules,
so a receiver states a concurrency contract: `&mut self` means "one caller
at a time", which is how `LinkAdapt` and `TagMonitor` replace the C++
recursive mutex. A `&self` method that changes state does it through a
`Mutex` or an atomic inside (interior mutability), as `Tag` does.

```rust
pub fn read_debug32(&mut self, addr: u32) -> Option<u32> {
```
```rust
pub fn status(&self) -> Result<Status, TagError> {
```

[target.rs:153](../../libraries/tagcore-rs/src/link/target.rs#L153),
[tag.rs:471](../../libraries/tagcore-rs/src/tag.rs#L471).
Explained in: [3, `&mut self` instead of a recursive mutex](03-usb-and-stlink.md#mut-self-instead-of-a-recursive-mutex);
[5, Tag: one mutex, `&self` everywhere](05-protobuf-and-the-tag-api.md#tag-one-mutex-self-everywhere).

### Slices and arrays: `[u8; 16]`, `&[u8]`, `&mut [u8]`

`[u8; 16]` is a fixed-size array and a value: it is copied, returned and
compared like a struct. `&[u8]` and `&mut [u8]` are slices, a pointer and a
length in one value (the C `(uint8_t *buf, size_t len)` pair, or
`std::span`), bounds-checked on every index. `&[u8; 16]` borrows exactly
sixteen bytes, so the type system checks the size.

```rust
pub(super) fn cmd_read(&mut self, cmd: &[u8; 16], buf: &mut [u8]) -> bool {
```

[stlink.rs:306](../../libraries/tagcore-rs/src/link/stlink.rs#L306).
Explained in: [3, The 16-byte command block](03-usb-and-stlink.md#the-16-byte-command-block);
[7, Memory through the base's own commands](07-programming-tags-and-bases.md#memory-through-the-bases-own-commands).

### `self` by value, and `self: Box<Self>`

A method taking `self` (no `&`) consumes the object: its fields are moved
out or dropped and the caller cannot use it afterwards. The compiler
enforces "nothing after `finish`", which C++ can only document.
`self: Box<Self>` does the same for an object that lives in a `Box`.

```rust
fn finish(self, name: &str) -> ExternalCaptureFile {
```
```rust
fn into_probe(self: Box<Self>) -> Box<dyn DebugProbe> {
```

[external.rs:135](../../libraries/tagcore-rs/src/recovery/external.rs#L135),
[fieldsafe.rs:283](../../libraries/tagprobe-rs/src/fieldsafe.rs#L283).
Explained in: [6, external.rs](06-recovery.md#externalrs-the-external-flash-hashed-as-it-is-written);
[7, From probe to ARM interface: consuming `self`](07-programming-tags-and-bases.md#from-probe-to-arm-interface-consuming-self).

### Lifetimes: `'static`, `'_`, `'a`

A lifetime names how long a borrow is valid. `&'static str` is a reference
valid for the whole program, which in practice means a string literal or a
`static`. `'_` is "a lifetime the compiler works out" and appears on types
that borrow (`Formatter<'_>`, `MutexGuard<'_, Inner>`). A named `'a` on a
struct says the struct holds a borrow and cannot outlive what it borrows;
C++ would hold a raw pointer and a comment.

```rust
pub struct CaptureOptions<'a> {
    // ...
    pub progress: Option<&'a mut dyn FnMut(&str)>,
}
```

[capture.rs:45](../../libraries/tagcore-rs/src/recovery/capture.rs#L45);
also `pub struct FieldSafeMemory<'a>` at
[fieldsafe.rs:625](../../libraries/tagprobe-rs/src/fieldsafe.rs#L625) and
`pub fn name(self) -> &'static str` at
[monitor/mod.rs:240](../../libraries/tagcore-rs/src/monitor/mod.rs#L240).
Explained in: [6, capture.rs: the order, and the options](06-recovery.md#capturers-the-order-and-the-options);
[6, The type, and its destructor](06-recovery.md#the-type-and-its-destructor);
[7, The memory interface borrows the ARM interface](07-programming-tags-and-bases.md#the-memory-interface-borrows-the-arm-interface);
[3, Failure runs](03-usb-and-stlink.md#failure-runs-one-error-per-burst);
[13, Hop 2](13-tracing-start.md#hop-2-the-shell).

### Generics and trait bounds: `<M: ReflectMessage>`, `where`

A generic function is a template; the bound after `:` is the concept the
type must satisfy, and the body may use only what the bound provides. Each
type used gets its own compiled copy (monomorphisation), as with a C++
template. A `where` clause is the same bound written after the signature.

```rust
pub fn debug_string<M: ReflectMessage>(msg: &M) -> String {
```
```rust
pub fn enum_name<E>(value: i32, name: fn(&E) -> &'static str) -> String
where
    E: TryFrom<i32>,
{
```

[proto.rs:49](../../libraries/tagcore-rs/src/proto.rs#L49),
[commandline-rs lib.rs:236](../../commandline-rs/src/lib.rs#L236).
Explained in: [5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter);
[9, Choosing a base](09-command-line-tools.md#choosing-a-base);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### The turbofish `::<T>`

`f::<T>(...)` names a generic argument explicitly, where C++ writes
`f<T>(...)`. It is needed when nothing in the arguments tells the compiler
the type, e.g. which managed state to fetch or what to parse into.

```rust
window.state::<Engine>().command("quit", json!({}));
```

[tagapp-shell lib.rs:311](../../libraries/tagapp-shell-rs/src/lib.rs#L311);
also `select_base::<Cli>(&tag, &cli.base)` at
[tag-dwnld.rs:385](../../commandline-rs/src/bin/tag-dwnld.rs#L385) and
`xyz.as_chunks::<3>()` at
[calibsession lib.rs:302](../../libraries/calibsession-rs/src/lib.rs#L302).
Explained in: [5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter);
[6, image.rs](06-recovery.md#imagers-reading-an-elf-with-object);
[9, Choosing a base](09-command-line-tools.md#choosing-a-base);
[10, run: the close hook](10-tauri-shell.md#run-the-close-hook-the-command-table-and-run).

### `impl Trait` in argument and return position

In an argument, `impl Trait` is a generic parameter written short: "any
type implementing `Trait`", compiled once per caller's type (a template
parameter, not a virtual call or a `std::function`). In a return type it
hides a concrete type the caller cannot name, typically an iterator or a
closure.

```rust
pub fn format_g(v: f64, short: usize, long: usize, round_trips: impl Fn(&str) -> bool) -> String {
```
```rust
pub fn record_offsets() -> impl Iterator<Item = u32> {
```

[textformat.rs:133](../../libraries/tagcore-rs/src/textformat.rs#L133),
[identity.rs:194](../../libraries/tagcore-rs/src/recovery/identity.rs#L194);
`fn fail(message: impl std::fmt::Display) -> ExitCode` at
[tag-dwnld.rs:151](../../commandline-rs/src/bin/tag-dwnld.rs#L151).
Explained in: [5, textformat.rs](05-protobuf-and-the-tag-api.md#textformatrs-debugstring-byte-for-byte);
[7, The program sequence](07-programming-tags-and-bases.md#the-program-sequence);
[9, The shape of every tool](09-command-line-tools.md#the-shape-of-every-tool);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn).

### `Self`, associated constants and local `const`

`Self` is the type the `impl` block is for. A `const` inside an `impl` is
an associated constant, the `static constexpr` class member, written
`Self::NAME` or `Type::NAME`; it can size an array type. A `const` inside a
function body is a local `constexpr`. `pub const LEGACY: Self = Self { .. }`
is a constant instance of the type.

```rust
const RPCBUF_BYTES: usize = 64 * 1024;
```
```rust
pub fn from_bytes(bytes: &[u8; Self::SIZE]) -> Self {
```

[monitor/mod.rs:345](../../libraries/tagcore-rs/src/monitor/mod.rs#L345),
[abi.rs:185](../../libraries/tagcore-rs/src/monitor/abi.rs#L185).
Explained in: [4, The struct](04-monitor-and-rpc.md#the-struct);
[4, The ABI, mirrored by hand](04-monitor-and-rpc.md#the-abi-mirrored-by-hand);
[4, The DebugMonitor call](04-monitor-and-rpc.md#the-debugmonitor-call-call_l4);
[4, Legacy BitTag rescue](04-monitor-and-rpc.md#legacy-bittag-rescue-monitorrescuers).

### `static` tables and `const fn`

A `static` initialised with a struct literal is built at compile time and
lives in read-only data, like a `const` aggregate in C; slice fields are
`&'static [T]` written inline as `&[ ... ]`. A `const fn` can run at
compile time, so it may initialise a `static`. `_` separates digits in a
number literal.

```rust
pub static STM32L432: McuMap = McuMap {
    name: "STM32L432",
    dev_id: 0x435,
    dbgmcu_idcode: 0xE004_2000,
```

[mcu.rs:94](../../libraries/tagcore-rs/src/recovery/mcu.rs#L94).
Explained in: [6, mcu.rs](06-recovery.md#mcurs-one-table-per-part);
[5, configshape.rs](05-protobuf-and-the-tag-api.md#configshapers-which-fields-a-tag-supports).

### Type aliases: `type`

`type` gives an existing type a second name, as `using` does in C++. It
creates no new type.

```rust
pub type AddressRange = (u32, u32);
```
```rust
type Sink = Arc<dyn Fn(Event) + Send + Sync>;
```

[session.rs:120](../../libraries/tagcore-rs/src/recovery/session.rs#L120),
[tagapp-engine lib.rs:235](../../libraries/tagapp-engine-rs/src/lib.rs#L235);
`type QueryFn = Arc<dyn Fn(&str, Value) -> Value + Send + Sync>;` at
[tagapp-shell lib.rs:69](../../libraries/tagapp-shell-rs/src/lib.rs#L69).
Explained in: [10, The engine as the shell sees it](10-tauri-shell.md#the-engine-as-the-shell-sees-it);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn).

### `?Sized`

Generic parameters are assumed to have a size known at compile time.
`W: ?Sized` lifts that, so `W` may also be an unsized type such as
`dyn Write` behind a reference. It appears where a library trait's method
signature requires it.

```rust
fn write_f32<W: ?Sized + std::io::Write>(&mut self, w: &mut W, v: f32) -> std::io::Result<()> {
```

[proto.rs:80](../../libraries/tagcore-rs/src/proto.rs#L80).
Explained in: [5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter).

## Ownership in practice

The rules are in [the guide](../guide/14-rust-for-this-codebase.md#ownership-is-the-design).
These are the moves you see the code make to live with them.

### Moves, `Copy` and `.clone()`

Assigning or passing a value moves it: the source can no longer be used,
and no copy constructor runs. A type that derives `Copy` (small plain data:
integers, `Driver`, `TargetFamily`) is copied bitwise instead, like a C
enum. Anything else is duplicated only by an explicit `.clone()`. Cloning
an `Arc` or a channel `Sender` copies a handle, not the data behind it.

```rust
let query = engine.query.clone();
```

[tagapp-shell lib.rs:166](../../libraries/tagapp-shell-rs/src/lib.rs#L166)
(`query` is an `Arc` around a closure, so this copies a pointer).
Explained in: [7, Choosing the driver](07-programming-tags-and-bases.md#choosing-the-driver-from-the-bases-identity);
[10, The two commands](10-tauri-shell.md#the-two-commands);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### A borrow ends at its last use

The borrow checker tracks a reference to its last use, not to the closing
brace ("non-lexical lifetimes"). Code may therefore borrow something, stop
using the borrow, and then move or mutate the original in the same block.
Reordering such lines can make them stop compiling, which is the checker
doing its job.

```rust
let text = |index: Option<u8>| {
    index
        .and_then(|i| handle.read_string_descriptor_ascii(i).ok())
        .unwrap_or_default()
};
// ...
self.handle = Some(handle);
```

[discovery.rs:230](../../libraries/tagcore-rs/src/link/discovery.rs#L230)
(the closure borrows `handle`; after its last call `handle` is moved).
Explained in: [3, Opening a probe](03-usb-and-stlink.md#opening-a-probe-and-choosing-the-endpoints);
[3, Bulk transfers and timeouts](03-usb-and-stlink.md#bulk-transfers-and-timeouts);
[4, When the firmware does not answer](04-monitor-and-rpc.md#when-the-firmware-does-not-answer).

### Borrowing two fields at once

Two fields of one struct may be borrowed mutably at the same time, because
they cannot alias. What is refused is calling a `&mut self` method, which
borrows the whole struct, while a borrow of one of its fields is alive.

```rust
let read_ok = len == 0
    || self
        .link
        .read_mem32(self.call_buf, &mut self.rpcbuf[..round4(len)]);
```

[rpc.rs:166](../../libraries/tagcore-rs/src/monitor/rpc.rs#L166).
Explained in: [4, The RPC](04-monitor-and-rpc.md#the-rpc).

### Copy a field out before a closure borrows the struct

The commonest fix for a borrow error: read the value you need into a local
first, then let a closure take the borrow of the whole struct.

```rust
let full = o.external_full;
let mut forward = |line: &str| progress(o, line);
```

[capture.rs:629](../../libraries/tagcore-rs/src/recovery/capture.rs#L629).
Explained in: [6, capture.rs](06-recovery.md#capturers-the-order-and-the-options).

### `std::mem::take` and `std::mem::replace`

`take(&mut x)` returns `x` and leaves `T::default()` in its place;
`replace(&mut x, v)` leaves `v`. Both are "read and reset" in one step, the
C++ `std::exchange`. On a `bool` they make a test-and-clear or a one-shot
latch.

```rust
if !std::mem::take(&mut self.l4_dma_clock_set) {
```
```rust
!std::mem::replace(&mut self.started, true)
```

[l4.rs:661](../../libraries/tagcore-rs/src/monitor/l4.rs#L661),
[tagapp-engine lib.rs:335](../../libraries/tagapp-engine-rs/src/lib.rs#L335).
Explained in: [4, The L4 attach](04-monitor-and-rpc.md#the-l4-attach);
[11, The engine loop](11-engine-and-model.md#the-engine-loop).

### `Option::take` and `Option::take_if`

`opt.take()` moves the value out and leaves `None`: the `Option` form of
`mem::take`. `take_if(pred)` does so only when the predicate holds; no code
in the tree uses it now (Tag Calibrator's prompt answer did, before it moved
onto the shared engine toolkit's prompt queue).

```rust
self.exit |= self.quitting.take().is_some();
```

[tagmonitor monitor.rs:722](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L722).
Explained in: [11, The engine loop](11-engine-and-model.md#the-engine-loop) (the quit deadline).

### `as_ref`, `as_deref`, `as_deref_mut`

These borrow through an `Option` (or `Result`) without moving its content.
`as_ref()` turns `&Option<T>` into `Option<&T>`; `as_deref()` goes one step
further, `Option<String>` to `Option<&str>` or `Option<PathBuf>` to
`Option<&Path>`. `as_deref_mut()` reborrows an `Option<&mut T>`, so an
optional callback or output can be used again on each loop iteration
instead of being moved out on the first.

```rust
let handle = self.handle.as_ref()?;
```
```rust
if let Some(f) = failed.as_deref_mut() {
```

[stlink.rs:429](../../libraries/tagcore-rs/src/link/stlink.rs#L429),
[session.rs:402](../../libraries/tagcore-rs/src/recovery/session.rs#L402).
Explained in: [6, Core control and reads](06-recovery.md#core-control-and-reads);
[7, Connecting](07-programming-tags-and-bases.md#connecting);
[9, Before attaching](09-command-line-tools.md#before-attaching).

### `Box<T>`, `*` and `&*`, and deref coercion

`Box<T>` owns a heap value (`std::unique_ptr`). In an enum it keeps a large
variant from making every value of the enum large. `*b` moves the value
out of the box; `&*b` borrows it as `&T`. Where a `&T` is expected, the
compiler also converts `&Box<T>` (or `&String`, `&Vec<T>`) to `&T`
automatically ("deref coercion"), but not when the parameter is generic.

```rust
Start(Box<Config>),
```
```rust
tagcore::proto::debug_string(&*config)
```

[tagthread.rs:54](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L54),
[tagthread.rs:258](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L258);
`self.load(*config, DocumentSource::Tag, "")` at
[monitor.rs:336](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L336).
Explained in: [5, TagError](05-protobuf-and-the-tag-api.md#tagerror-an-error-written-by-hand);
[11, The messages](11-engine-and-model.md#the-messages);
[11, The tag thread](11-engine-and-model.md#the-tag-thread);
[13, The tag thread](13-tracing-start.md#the-tag-thread).

### `drop(x)` and `std::mem::forget`

`drop(x)` ends a value's life now, running its destructor, like
`ptr.reset()`; it is how RAII cleanup is forced before a later statement.
`mem::forget(x)` takes the value and never runs its destructor, a
deliberate leak.

```rust
drop(core);
drop(session);
```

[tagprobe lib.rs:647](../../libraries/tagprobe-rs/src/lib.rs#L647);
`std::mem::forget(token);` at
[tagapp-shell lib.rs:220](../../libraries/tagapp-shell-rs/src/lib.rs#L220).
Explained in: [7, The other session users](07-programming-tags-and-bases.md#the-other-session-users);
[10, The user guide and App Nap](10-tauri-shell.md#the-user-guide-and-app-nap).

### Shadowing

A second `let` with the same name makes a new variable, often of a new
type, and hides the old one. The code uses it for successive conversions of
one value, with let-else to rebind an `Option` as its content, and with
`#[cfg]` to extend a builder on one platform only (`let builder =
builder.menu(...)` under `#[cfg(target_os = "macos")]` in the Tauri shell,
[chapter 10](10-tauri-shell.md#run-building-the-application)).

```rust
let path = path
    .to_str()
    .ok_or_else(|| WriterError(format!("Path is not UTF-8: {}", path.display())))?;
let path = CString::new(path)
    .map_err(|_| WriterError(format!("Path contains a NUL byte: {path}")))?;
```

[taglogwriter lib.rs:185](../../libraries/taglogwriter-rs/src/lib.rs#L185).
Explained in: [8, Creating it](08-bridges-to-cpp-and-python.md#creating-it);
[9, The page loop](09-command-line-tools.md#the-page-loop).

### Changing state through a shared reference: `Cell`, `RefCell`, `Rc`

`Cell<T>` and `RefCell<T>` allow mutation through a `&` on one thread:
`Cell` by copying values in and out, `RefCell` by handing out borrows
checked at run time (a second `borrow_mut` panics). `Rc<T>` is a
single-threaded reference count, `std::shared_ptr` without the atomics.
They are rare here: a DFU progress counter shared with a `'static` closure,
and the per-thread record list in a test logger.

```rust
let written = std::rc::Rc::new(std::cell::Cell::new(0usize));
let counter = written.clone();
```

[dfu.rs:278](../../libraries/tagprobe-rs/src/dfu.rs#L278);
`RefCell` at [link/mod.rs:602](../../libraries/tagcore-rs/src/link/mod.rs#L602).
Explained in: [7, Writing](07-programming-tags-and-bases.md#writing-and-treating-the-departure-as-success);
[3, Failure runs](03-usb-and-stlink.md#failure-runs-one-error-per-burst).

## Errors and absence

The model is in [the guide](../guide/14-rust-for-this-codebase.md#errors-result-and-option):
no exceptions; `Result<T, E>` for what can fail, `Option<T>` for what may be
absent.

### `?` on a `Result`, and the `From` conversion

`expr?` returns early with the error if `expr` is `Err`, and otherwise
yields the `Ok` value: the C error check after every call, as one
character. On the way out it converts the error with `From::from`, so a
`&str` error becomes the function's `String` error with no `.into()`.

```rust
let m = s.mcu().ok_or("no recovery session")?;
```

[rescue.rs:96](../../libraries/tagcore-rs/src/recovery/rescue.rs#L96).
Explained in: [6, Errors are strings](06-recovery.md#errors-are-strings);
[7, The session](07-programming-tags-and-bases.md#the-session-target-debug-sequence-attach-under-reset);
[7, Opening, and the two async steps](07-programming-tags-and-bases.md#opening-and-the-two-async-steps);
[9, Settle](09-command-line-tools.md#settle).

### `?` on an `Option`

In a function returning `Option`, `x?` returns `None` if `x` is `None`, and
otherwise unwraps it: "if no device is open, return nothing".

```rust
let handle = self.handle.as_ref()?;
```

[stlink.rs:429](../../libraries/tagcore-rs/src/link/stlink.rs#L429).
Explained in: [3, Base identify](03-usb-and-stlink.md#base-identify-f6-99);
[11, The configuration editor](11-engine-and-model.md#the-configuration-editor);
[13, Hop 4](13-tracing-start.md#hop-4-the-configuration).

### `?` inside a closure

Inside a closure, `?` returns from the closure, not from the enclosing
function. With `find_map` or `filter_map`, a `None` from `?` means "not
this one, try the next".

```rust
self.segments.iter().find_map(|s| {
    let start = addr.checked_sub(s.addr)? as usize;
    s.data.get(start..start.checked_add(len)?)
})
```

[image.rs:144](../../libraries/tagcore-rs/src/recovery/image.rs#L144).
Explained in: [6, image.rs](06-recovery.md#imagers-reading-an-elf-with-object);
[10, main](10-tauri-shell.md#main);
[11, The logger](11-engine-and-model.md#the-logger);
[14, 100 ms samples](14-the-three-applications.md#100-ms-samples-200-ms-quality).

### let-else

`let PATTERN = expr else { ... };` binds the pattern's names if it matches;
otherwise the `else` block runs, and it must leave the scope (`return`,
`break`, `continue`, `panic!`). It is the guard clause
`if (!opt) return {}; auto x = *opt;` in one statement, without nesting the
rest of the function.

```rust
let Ok(count) = u16::try_from(buf.len()) else {
    error!("read of {} bytes exceeds one ST-LINK transfer", buf.len());
    return false;
};
```

[target.rs:75](../../libraries/tagcore-rs/src/link/target.rs#L75);
with a tuple of `Option`s at
[xflash.rs:193](../../libraries/tagcore-rs/src/recovery/xflash.rs#L193).
Explained in: [1, Linking a CMake product](01-workspace-and-build.md#linking-a-cmake-product-tagcore-c-sys-and-links);
[3, Discovery](03-usb-and-stlink.md#discovery-usbdev-and-available);
[5, configshape.rs](05-protobuf-and-the-tag-api.md#configshapers-which-fields-a-tag-supports);
[6, sramcall.rs](06-recovery.md#sramcallrs-calling-code-in-sram);
[7, Listing](07-programming-tags-and-bases.md#listing);
[13, TagMonitor::rpc](13-tracing-start.md#tagmonitorrpc-encode-write-call-read-decode).

### `if let` and `while let`

`if let PATTERN = expr { ... }` is a one-arm `match`: the block runs only if
the pattern matches, with its names bound. `while let` loops while it
matches, so "read until the tag refuses" is one line.

```rust
while let Ok(constants) = tag.read_calibration(calibration.len() as i32) {
    calibration.push(constants);
}
```

[tag-dwnld.rs:231](../../commandline-rs/src/bin/tag-dwnld.rs#L231).
Explained in: [7, DAP registers from a table](07-programming-tags-and-bases.md#dap-registers-from-a-table);
[9, main](09-command-line-tools.md#main);
[9, The header](09-command-line-tools.md#the-header).

### Let chains

`if let A = x && cond && let B = y { ... }` joins pattern matches and
boolean conditions with `&&`; each `let` binds names that the later
conditions and the body can use, and the body runs only if all hold. New in
edition 2024 (Rust 1.88); C++ needs nested `if`s.

```rust
if let Some(stopped) = self.stop().map_err(StopFailure::Refused)?
    && is_stopped(stopped.state)
{
    return Ok(stopped);
}
```

[tag.rs:647](../../libraries/tagcore-rs/src/tag.rs#L647);
a three-part chain at [l4.rs:471](../../libraries/tagcore-rs/src/monitor/l4.rs#L471).
Explained in: [4, Attach](04-monitor-and-rpc.md#attach);
[5, stop_confirmed](05-protobuf-and-the-tag-api.md#stop_confirmed);
[8, The rpath](08-bridges-to-cpp-and-python.md#the-rpath-only-the-final-binary-can-set-it);
[10, run: the close hook](10-tauri-shell.md#run-the-close-hook-the-command-table-and-run);
[11, Prompts](11-engine-and-model.md#prompts-dialogs-as-data);
[14, 100 ms samples](14-the-three-applications.md#100-ms-samples-200-ms-quality).

### Option and Result combinators

Methods on `Option` and `Result` that replace small `if`s. Each passes
"nothing" or the error along untouched, so a chain needs one fallback at
the end. Read `opt.map(f)` as `opt ? f(*opt) : nullopt`.

| Method | Does | Example |
| --- | --- | --- |
| `map(f)` | Applies `f` to the `Some`/`Ok` value | `.map(str::to_string)`, [rescue.rs:122](../../libraries/tagcore-rs/src/recovery/rescue.rs#L122) |
| `and_then(f)` | Chains a step that itself returns `Option`/`Result` | `.and_then(\|t\| t.get("uid"))`, [rescue.rs:119](../../libraries/tagcore-rs/src/recovery/rescue.rs#L119) |
| `or_else(f)`, `or(b)` | The first if present, otherwise try `f()` or `b` | `.or_else(\|\| ty.get_field_by_json_name(key))`, [configshape.rs:102](../../libraries/tagcore-rs/src/configshape.rs#L102) |
| `filter(p)` | `Some` becomes `None` if `p` fails | `.filter(\|u\| !u.is_empty())`, [rescue.rs:121](../../libraries/tagcore-rs/src/recovery/rescue.rs#L121) |
| `ok()` | `Result` to `Option`, dropping the error | `.ok()?`, [stlink.rs:435](../../libraries/tagcore-rs/src/link/stlink.rs#L435) |
| `ok_or(e)`, `ok_or_else(f)` | `Option` to `Result`, `None` becoming `Err(e)` | [rescue.rs:123](../../libraries/tagcore-rs/src/recovery/rescue.rs#L123) |
| `map_err(f)` | Converts or logs the error | `ack.map_err(\|e\| error!("Reply did not decode: {e}")).ok()`, [rpc.rs:187](../../libraries/tagcore-rs/src/monitor/rpc.rs#L187) |
| `unwrap_or(v)`, `unwrap_or_default()`, `unwrap_or_else(f)` | The value, or a fallback | `.unwrap_or(Duration::from_secs(3600))`, [tagthread.rs:342](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L342) |
| `map_or(d, f)` | `f(value)`, or `d` | `.map_or(0, \|f\| f.value & 0xFFFF)`, [capture.rs:574](../../libraries/tagcore-rs/src/recovery/capture.rs#L574) |
| `is_some_and(p)`, `is_ok_and(p)`, `is_none_or(p)` | Test presence and content in one call | `.is_some_and(\|id\| id & L4_DBGMCU_DEV_ID_MASK == L4_DEV_ID_L43X)`, [l4.rs:474](../../libraries/tagcore-rs/src/monitor/l4.rs#L474) |
| `transpose()` | `Option<Result<T, E>>` to `Result<Option<T>, E>` | [tag-dwnld.rs:379](../../commandline-rs/src/bin/tag-dwnld.rs#L379) |
| `copied()`, `cloned()` | `Option<&T>` to `Option<T>` | `panic.downcast_ref::<String>().cloned()`, [station.rs:510](../../applications/tagprogram-rs/core/src/engine/station.rs#L510) |
| `bool::then(f)` | `Some(f())` if true, else `None` | `self.cmd_read(&cmd, &mut result).then(\|\| unpack16(&result))`, [stlink.rs:353](../../libraries/tagcore-rs/src/link/stlink.rs#L353) |

A longer chain, from [rescue.rs:118](../../libraries/tagcore-rs/src/recovery/rescue.rs#L118):

```rust
    v.get("target")
        .and_then(|t| t.get("uid"))
        .and_then(|u| u.as_str())
        .filter(|u| !u.is_empty())
        .map(str::to_string)
        .ok_or_else(|| format!("{} records no target.uid", path.display()))
```

Explained in: [3, The struct](03-usb-and-stlink.md#the-struct);
[3, Opening a probe](03-usb-and-stlink.md#opening-a-probe-and-choosing-the-endpoints);
[3, The 16-byte command block](03-usb-and-stlink.md#the-16-byte-command-block);
[5, configshape.rs](05-protobuf-and-the-tag-api.md#configshapers-which-fields-a-tag-supports);
[7, PEMPTY and the start](07-programming-tags-and-bases.md#pempty-and-the-start);
[7, base-flash](07-programming-tags-and-bases.md#base-flash-confirming-by-identity);
[8, Calling back into Python](08-bridges-to-cpp-and-python.md#calling-back-into-python-from-detached-code);
[9, Before attaching](09-command-line-tools.md#before-attaching);
[11, The engine loop](11-engine-and-model.md#the-engine-loop);
[11, Settings](11-engine-and-model.md#settings-qsettings-as-a-json-file);
[14, The session step machine](14-the-three-applications.md#the-session-step-machine).

### `let _ =`: discarding a result on purpose

`Result` is marked `#[must_use]`, so ignoring one draws a warning.
`let _ = expr;` says "ignored deliberately", e.g. for a `write!` into a
`String` that cannot fail, or a channel send during shutdown.

```rust
let _ = writeln!(j, "  \"reason\": {},", json_string(&o.reason));
```

[capture.rs:406](../../libraries/tagcore-rs/src/recovery/capture.rs#L406).
Explained in: [6, The manifest](06-recovery.md#the-manifest-written-by-hand);
[9, Ctrl-C and SIGPIPE](09-command-line-tools.md#ctrl-c-and-sigpipe);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn);
[13, Hop 2](13-tracing-start.md#hop-2-the-shell).

### `unwrap`, `expect` and `panic!`

`unwrap()` takes the value out of an `Option` or `Result` and panics if
there is none; `expect("msg")` does the same with a message. A panic
unwinds the thread, running destructors, like an uncaught exception. The
code uses them only for invariants (a 4-byte slice is 4 bytes) and in
build scripts, where aborting is the error report.

```rust
.expect("embedded file descriptor set decodes")
```

[proto.rs:32](../../libraries/tagcore-rs/src/proto.rs#L32).
Explained in: [1, The Tauri applications](01-workspace-and-build.md#the-tauri-applications-staging-the-page);
[10, build.rs](10-tauri-shell.md#buildrs-staging-the-page).

### Error types written by hand

An error type is an ordinary struct or enum. Implementing `Display` gives
it a message (`{e}`, `.to_string()`); an empty
`impl std::error::Error` marks it as an error. This tree uses no error
crates. Lower layers often use `Result<T, String>`, a message and nothing
more, instead of defining a type.

```rust
impl fmt::Display for TagError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for TagError {}
```

[tag.rs:75](../../libraries/tagcore-rs/src/tag.rs#L75).
Explained in: [5, TagError](05-protobuf-and-the-tag-api.md#tagerror-an-error-written-by-hand);
[6, Errors are strings](06-recovery.md#errors-are-strings);
[7, The probe](07-programming-tags-and-bases.md#the-probe).

### `catch_unwind`

`std::panic::catch_unwind(f)` runs `f` and turns a panic into an `Err`
holding the panic's payload, like `catch (...)`. `downcast_ref::<T>()` is
a checked `dynamic_cast` of that payload to recover its message.

```rust
let done: Done = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(work)) {
    Ok(result) => Box::new(move |s: &mut Station| apply(s, result)),
```

[station.rs:504](../../applications/tagprogram-rs/core/src/engine/station.rs#L504).
Explained in: [14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

## Enums and matching

The model is in [the guide](../guide/14-rust-for-this-codebase.md#enums-carry-data-and-match-must-be-complete).

### Enums that carry data

Each variant of a Rust enum may carry its own fields: a tagged union the
compiler manages, a `std::variant` of one struct per case. The fields are
reachable only after a `match` or `if let` has checked the variant.

```rust
pub(crate) enum TagCmd {
    /// List the connected bases.
    FindBases,
    /// Attach to the tag on this base (resets it).
    Attach(UsbDev),
    // ...
```

[tagthread.rs:44](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L44);
a variant with named fields, `Range { start: u64, end: u64 }`, at
[tagprobe lib.rs:408](../../libraries/tagprobe-rs/src/lib.rs#L408).
Explained in: [7, The program sequence](07-programming-tags-and-bases.md#the-program-sequence);
[9, The flashing tools](09-command-line-tools.md#the-flashing-tools-tag-flash-and-base-flash);
[11, The messages](11-engine-and-model.md#the-messages).

### `match` is an expression, and must be exhaustive

A `match` has a value, so it can sit on the right of `=` or in the middle
of an expression; an arm may be a block whose last expression is its
value. Every possible value must be covered: `_` is the default arm, and a
new enum variant breaks every `match` that does not handle it. Arms are
tried in order.

```rust
self.ep_out = match desc.product_id() {
    stlink::USB_PID_STLINK_NUCLEO
    // ...
    | stlink::USB_PID_STLINK_V3_PWR => 1,
    _ => 2,
} | rusb::constants::LIBUSB_ENDPOINT_OUT;
```

[discovery.rs:219](../../libraries/tagcore-rs/src/link/discovery.rs#L219).
Explained in: [3, Opening a probe](03-usb-and-stlink.md#opening-a-probe-and-choosing-the-endpoints);
[4, The L4 attach](04-monitor-and-rpc.md#the-l4-attach);
[11, Commands and their availability](11-engine-and-model.md#commands-and-their-availability).

### `if` and blocks are expressions

`if c { a } else { b }` has a value, like `?:` with blocks; so does a
`{ ... }` block. The last expression without a semicolon is the value. A
`return` inside such an expression leaves the whole function.

```rust
let chunk = if chunk == 0 || !chunk.is_multiple_of(BASE_PIECE) || chunk > MAX_CHUNK {
    4096
} else {
    chunk
} as usize;
```

[session.rs:385](../../libraries/tagcore-rs/src/recovery/session.rs#L385).
Explained in: [3, Memory, and the last-R/W status](03-usb-and-stlink.md#memory-and-the-last-rw-status);
[9, Printing every field](09-command-line-tools.md#printing-every-field-by-reflection);
[9, Settle](09-command-line-tools.md#settle).

### Or-patterns and constant patterns

`A | B | C` in a pattern matches any of them. A named `const` may appear in
a pattern and matches its value. Together they are a `switch` with
fall-through cases, as one expression.

```rust
&& matches!(
    pid,
    USB_PID_STLINK_32L
        | USB_PID_STLINK_32L_AUDIO
        | USB_PID_STLINK_NUCLEO
```

[discovery.rs:52](../../libraries/tagcore-rs/src/link/discovery.rs#L52).
Explained in: [3, Discovery](03-usb-and-stlink.md#discovery-usbdev-and-available);
[4, The mailbox call](04-monitor-and-rpc.md#the-mailbox-call-call_u3).

### Match guards

`PATTERN if condition =>` matches only when the condition also holds. An
unguarded arm for the same pattern below it takes the rest. `_ if cond`
can intercept every remaining value at one point in the list, which a C++
`switch` cannot express.

```rust
_ if !attached => {}
```
```rust
After::Quit if self.model.attached() || self.attaching => {
```

[tagthread.rs:247](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L247),
[monitor.rs:339](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L339).
Explained in: [3, Entering SWD](03-usb-and-stlink.md#entering-swd-with-a-retry-the-c-did-not-have);
[4, The DebugMonitor call](04-monitor-and-rpc.md#the-debugmonitor-call-call_l4);
[6, rescue.rs](06-recovery.md#rescuers-erase-only-what-has-been-captured);
[11, The tag thread](11-engine-and-model.md#the-tag-thread);
[11, Prompts](11-engine-and-model.md#prompts-dialogs-as-data).

### Binding catch-alls

A plain name as the last pattern matches anything and binds it, a default
arm that keeps the value.

```rust
match self.call.wait_return(s, Duration::from_millis(1000))? {
    1 => Ok(()),
    r0 => Err(format!("Serve() returned {r0}")),
}
```

[xflash.rs:451](../../libraries/tagcore-rs/src/recovery/xflash.rs#L451).
Explained in: [5, textformat.rs](05-protobuf-and-the-tag-api.md#textformatrs-debugstring-byte-for-byte);
[6, A command](06-recovery.md#a-command);
[9, Printing every field](09-command-line-tools.md#printing-every-field-by-reflection).

### Matching tuples

`match (a, b)` matches two values at once, a `switch` on a pair. It picks
one of several combinations, or chooses which of two results to report.

```rust
match (device_id, flash_bytes) {
    (DEV_L432, None | Some(0x40000)) => Ok("STM32L432KC"),
    (DEV_L432, Some(0x20000)) => Ok("STM32L432KB"),
    // ...
    (id, None) => Err(format!("no probe-rs target for device 0x{id:03x}")),
}
```

[tagprobe lib.rs:237](../../libraries/tagprobe-rs/src/lib.rs#L237);
`match (read, closed)` at
[external.rs:193](../../libraries/tagcore-rs/src/recovery/external.rs#L193).
Explained in: [6, external.rs](06-recovery.md#externalrs-the-external-flash-hashed-as-it-is-written);
[7, Choosing the probe-rs target](07-programming-tags-and-bases.md#choosing-the-probe-rs-target);
[9, The output](09-command-line-tools.md#the-output-and-the-bittag-rescue-layout);
[11, The engine loop](11-engine-and-model.md#the-engine-loop).

### Nested patterns, `..`, and matching through a reference

A pattern can reach inside several layers at once:
`Some(ack::Payload::Status(status))` matches an `Option` holding that
variant and binds the `Status` inside. `{ api, .. }` binds one field and
ignores the rest. Matching on `&value` binds the fields as references, so
nothing moves out; `*field` copies a `Copy` field out.

```rust
match inner.payload(&request(req::Payload::GetStatus(Empty {}))) {
    Some(ack::Payload::Status(status)) => Ok(status),
    _ => Err(inner.missing("status")),
}
```

[tag.rs:473](../../libraries/tagcore-rs/src/tag.rs#L473);
`match &cli.command` with `*erase` at
[tag-flash.rs:449](../../commandline-rs/src/bin/tag-flash.rs#L449).
Explained in: [5, One call, end to end](05-protobuf-and-the-tag-api.md#one-call-end-to-end-tagstatus);
[7, The settle pause](07-programming-tags-and-bases.md#the-settle-pause);
[13, The reply](13-tracing-start.md#the-reply);
[13, The model settles the start](13-tracing-start.md#the-model-settles-the-start).

### `matches!`

`matches!(value, PATTERN)` is `true` if the value fits the pattern, which
may have or-patterns and a guard: a one-line `match` returning a `bool`.

```rust
if !matches!(stop, Some(s) if s != 0) {
```

[monitor/mod.rs:706](../../libraries/tagcore-rs/src/monitor/mod.rs#L706).
Explained in: [3, Discovery](03-usb-and-stlink.md#discovery-usbdev-and-available);
[4, Detach](04-monitor-and-rpc.md#detach);
[7, The settle pause](07-programming-tags-and-bases.md#the-settle-pause);
[11, Commands and their availability](11-engine-and-model.md#commands-and-their-availability).

### A variant constructor used as a function

A tuple variant's name is a function from its contents to the enum, so
`StopFailure::Status` can be passed where a `fn(TagError) -> StopFailure`
is wanted; `Err` and `Some` work the same way.

```rust
let status = self.status().map_err(StopFailure::Status)?;
```

[tag.rs:640](../../libraries/tagcore-rs/src/tag.rs#L640);
`self.error.map_or(Ok(()), Err)` at
[tagcore-py lib.rs:171](../../python/tagcore-py/src/lib.rs#L171).
Explained in: [5, stop_confirmed](05-protobuf-and-the-tag-api.md#stop_confirmed);
[8, Calling back into Python](08-bridges-to-cpp-and-python.md#calling-back-into-python-from-detached-code);
[9, Stop](09-command-line-tools.md#stop-stop_confirmed).

### `#[default]` on a variant

With `#[derive(Default)]` on an enum, `#[default]` marks the variant
`Default::default()` returns.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TargetFamily {
    /// Not attached, or the IDCODE matched neither family; the L4 transport
    /// is tried.
    #[default]
    Unknown,
```

[monitor/mod.rs:180](../../libraries/tagcore-rs/src/monitor/mod.rs#L180).
Explained in: [4, The struct](04-monitor-and-rpc.md#the-struct).

### Wire values: integer constants, `as i32` and `try_from`

A Rust enum may hold only its declared variants; it is not "an int with
names", and a stray integer in one is undefined behaviour. So values read
off the wire stay integers: the monitor ABI uses modules of constants, the
C bridge uses `pub const`s, and a prost enum field is an `i32`. `X as i32`
gives a prost enum's number; `TagState::try_from(n)` converts back and
returns `Err` for an unknown number.

```rust
pub mod status {
    /// No command in progress.
    pub const IDLE: u32 = 0;
```
```rust
TagState::try_from(state)
    .map(|s| s.as_str_name().to_owned())
    .unwrap_or_else(|_| state.to_string())
```

[abi.rs:61](../../libraries/tagcore-rs/src/monitor/abi.rs#L61),
[tag.rs:152](../../libraries/tagcore-rs/src/tag.rs#L152);
`st.state == TagState::Idle as i32` at
[session.rs:358](../../applications/tagprogram-rs/core/src/engine/session.rs#L358).
Explained in: [4, The ABI, mirrored by hand](04-monitor-and-rpc.md#the-abi-mirrored-by-hand);
[5, What the generated types look like](05-protobuf-and-the-tag-api.md#what-the-generated-types-look-like);
[8, Declaring C in Rust](08-bridges-to-cpp-and-python.md#declaring-c-in-rust).

### A protobuf `oneof` is an `Option` of an enum

prost generates a `oneof` as a field `payload: Option<m::Payload>`, where
`Payload` has one variant per case carrying its message. `None` is "not
set"; the C++ `payload_case()` switch plus `mutable_x()` becomes a `match`.

```rust
match &self.payload {
    Some(ack::Payload::ErrorMessage(message)) => message,
    _ => "",
}
```

[proto.rs:40](../../libraries/tagcore-rs/src/proto.rs#L40).
Explained in: [4, Keep-alive](04-monitor-and-rpc.md#keep-alive);
[5, What the generated types look like](05-protobuf-and-the-tag-api.md#what-the-generated-types-look-like);
[13, The reply](13-tracing-start.md#the-reply).

## Numbers, bytes and strings

### No implicit integer conversions: `From`, `try_from`, `as`

Rust never converts between integer types silently, and `<<` aside,
operators need both sides of one type. Three spellings, each a statement
of intent: `u64::from(x)` widens and cannot fail (it exists only for
lossless conversions); `u16::try_from(x)` narrows and returns `Err` if the
value does not fit; `x as u32` truncates or reinterprets bits, so an `as`
in this code means someone chose that.

```rust
((operand as u32) << 8) | u32::from(operation)
```

[abi.rs:127](../../libraries/tagcore-rs/src/monitor/abi.rs#L127);
`u16::try_from(buf.len())` at
[target.rs:75](../../libraries/tagcore-rs/src/link/target.rs#L75).
Explained in: [3, Memory, and the last-R/W status](03-usb-and-stlink.md#memory-and-the-last-rw-status);
[4, The ABI, mirrored by hand](04-monitor-and-rpc.md#the-abi-mirrored-by-hand);
[5, The downloads](05-protobuf-and-the-tag-api.md#the-downloads-data_log-calibration_log-state_log);
[6, mcu.rs](06-recovery.md#mcurs-one-table-per-part);
[8, Writing a page](08-bridges-to-cpp-and-python.md#writing-a-page).

### `bool` to an integer

`bool` and integers are distinct types; `u8::from(b)` (or
`c_int::from(b)`) gives 0 or 1.

```rust
u8::from(f.is_some()),
```

[l4.rs:271](../../libraries/tagcore-rs/src/monitor/l4.rs#L271).
Explained in: [3, nRST](03-usb-and-stlink.md#nrst-assert_resettrue-releases-reset);
[6, open](06-recovery.md#open);
[8, Creating it](08-bridges-to-cpp-and-python.md#creating-it).

### Overflow: `saturating_*`, `wrapping_*`, `checked_*`

Plain `+` panics on overflow in a debug build and wraps in release, so
arithmetic that may overflow says what it wants: `saturating_add` stops at
the limit, `wrapping_add` wraps (a sequence number), `checked_sub` returns
`None`. `is_multiple_of(n)` is `x % n == 0` without the division-by-zero
case.

```rust
self.failure_run = self.failure_run.saturating_add(1);
```

[link/mod.rs:526](../../libraries/tagcore-rs/src/link/mod.rs#L526);
`self.seq.wrapping_add(1)` at
[xflash.rs:296](../../libraries/tagcore-rs/src/recovery/xflash.rs#L296).
Explained in: [3, Failure runs](03-usb-and-stlink.md#failure-runs-one-error-per-burst);
[6, A command](06-recovery.md#a-command);
[6, image.rs](06-recovery.md#imagers-reading-an-elf-with-object).

### Bitwise NOT is `!`, and ranges

There is no `~`: `!` is bitwise NOT on an integer and logical NOT on a
`bool`. `a..b` is a half-open range and `a..=b` an inclusive one, used in
`for` loops, slices and `contains`.

```rust
let start = addr & !3;
```
```rust
if !(DEBUGVERSION..=DEBUGVERSION_MAILBOX).contains(&self.version)
```

[tagcore-py lib.rs:196](../../python/tagcore-py/src/lib.rs#L196),
[u3.rs:458](../../libraries/tagcore-rs/src/monitor/u3.rs#L458).
Explained in: [4, The L4 attach](04-monitor-and-rpc.md#the-l4-attach);
[4, The U3 attach](04-monitor-and-rpc.md#the-u3-attach);
[6, open](06-recovery.md#open);
[7, Memory through the base's own commands](07-programming-tags-and-bases.md#memory-through-the-bases-own-commands).

### Bytes: `to_le_bytes`, `from_le_bytes`, `try_into`, `copy_from_slice`

No struct is ever cast over a byte buffer. Multi-byte fields are packed
with `to_le_bytes()` and unpacked with `from_le_bytes()`, so host
endianness and padding never matter. A slice has a run-time length, so
turning `&b[o..o + 4]` into a `[u8; 4]` is `try_into()`, fallible in the
type system and unwrapped where the range guarantees the length.
`copy_from_slice` is a length-checked `memcpy`; `f32::from_bits` is the
safe type pun.

```rust
cmd[6..8].copy_from_slice(&count.to_le_bytes());
```
```rust
u32::from_le_bytes(b[o..o + 4].try_into().unwrap())
```

[target.rs:80](../../libraries/tagcore-rs/src/link/target.rs#L80),
[identity.rs:227](../../libraries/tagcore-rs/src/recovery/identity.rs#L227).
Explained in: [3, The 16-byte command block](03-usb-and-stlink.md#the-16-byte-command-block);
[4, The ABI, mirrored by hand](04-monitor-and-rpc.md#the-abi-mirrored-by-hand);
[6, sramcall.rs](06-recovery.md#sramcallrs-calling-code-in-sram);
[6, identity.rs](06-recovery.md#identityrs-the-identity-record).

### Strings: `String`, `&str`, `.into()`, lossy conversions

`String` is an owned, growable UTF-8 buffer (`std::string`); `&str` a
borrowed view (`std::string_view`), and a literal is a `&'static str`.
`"text".into()` or `.to_owned()` makes a `String` where one is wanted.
Bytes that may not be UTF-8 go through `String::from_utf8_lossy`, which
substitutes rather than fails; `into_owned()` turns its borrowed-or-owned
result (`Cow<str>`) into a `String`.

```rust
String::from_utf8_lossy(&self.sha_str[..end]).into_owned()
```

[monitor/mod.rs:416](../../libraries/tagcore-rs/src/monitor/mod.rs#L416);
`return Err("Cannot open the base".into());` at
[tagprobe lib.rs:271](../../libraries/tagprobe-rs/src/lib.rs#L271).
Explained in: [3, Base identify](03-usb-and-stlink.md#base-identify-f6-99);
[6, open](06-recovery.md#open);
[7, The connect check](07-programming-tags-and-bases.md#the-connect-check-without-probe-rs);
[8, Returned strings are copied at once](08-bridges-to-cpp-and-python.md#returned-strings-are-copied-at-once).

### Format strings

`format!`, `println!`, `write!` and the `log` macros take a format string
checked at compile time. `{}` uses `Display`, `{:?}` uses `Debug`; a name
inside the braces prints that variable (`{mode:x}`); after the colon come
printf-like specs: `x` hex, `08x` zero-padded, `#04x` with `0x`, `<5`
left-aligned. `{{` and `}}` are literal braces.

```rust
"no probe-rs target for device 0x{id:03x} with {} KB of flash",
```

[tagprobe lib.rs:243](../../libraries/tagprobe-rs/src/lib.rs#L243).
Explained in: [3, Attach](03-usb-and-stlink.md#attach-claim-reset-enter-swd);
[4, The struct](04-monitor-and-rpc.md#the-struct);
[5, The embedded default configs](05-protobuf-and-the-tag-api.md#the-embedded-default-configs);
[7, Choosing the probe-rs target](07-programming-tags-and-bases.md#choosing-the-probe-rs-target);
[9, Logging](09-command-line-tools.md#logging-in-the-logc-format).

### Literals: raw strings and byte strings

`r#"..."#` is a raw string: no escapes, so JSON fits without backslashes.
`b"base="` is a byte string, a `&[u8; 5]`, not a `&str`.

```rust
r#"{{"build":{{"frontendDist":{}}}}}"#,
```
```rust
let at = image.windows(5).position(|w| w == b"base=")?;
```

[tagapp-shell-build lib.rs:154](../../libraries/tagapp-shell-build-rs/src/lib.rs#L154),
[base-flash.rs:156](../../commandline-rs/src/bin/base-flash.rs#L156).
Explained in: [1, The Tauri applications](01-workspace-and-build.md#the-tauri-applications-staging-the-page);
[5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter);
[7, base-flash](07-programming-tags-and-bases.md#base-flash-confirming-by-identity);
[10, build.rs](10-tauri-shell.md#buildrs-staging-the-page).

## Structs, `derive` and `Default`

### Tuple structs, newtypes and unit structs

A tuple struct has unnamed fields read as `.0`, `.1`; with one field it is
a newtype, a distinct type wrapping another. A unit struct has no fields at
all; its one value is written as its name, and it exists to implement a
trait (a stateless class deriving from an interface).

```rust
pub struct WriterError(pub String);
```
```rust
pub struct TagSequence;

impl ArmDebugSequence for TagSequence {}
```

[taglogwriter lib.rs:114](../../libraries/taglogwriter-rs/src/lib.rs#L114),
[tagprobe lib.rs:304](../../libraries/tagprobe-rs/src/lib.rs#L304).
Explained in: [7, Opening the probe](07-programming-tags-and-bases.md#opening-the-probe);
[7, The probe](07-programming-tags-and-bases.md#the-probe);
[11, The logger](11-engine-and-model.md#the-logger).

### `#[derive(...)]`

`derive` asks the compiler (or a library's macro) to write trait
implementations from the type's definition. Nothing is copyable, printable
or comparable by default; each type opts in.

| Derive | Gives | C++ counterpart |
| --- | --- | --- |
| `Debug` | `{:?}` formatting | a generated `operator<<` for debugging |
| `Clone` | `.clone()` | copy constructor, called explicitly |
| `Copy` | implicit bitwise copy instead of move | a trivially copyable type |
| `PartialEq`, `Eq` | `==`, `!=` | defaulted `operator==` |
| `PartialOrd`, `Ord`, `Hash` | ordering, use as a map key | `operator<=>`, `std::hash` |
| `Default` | `T::default()`: zeros, empties, the `#[default]` variant | default constructor |
| `Parser`, `Args`, `Subcommand`, `ValueEnum` (clap) | a command-line parser from the struct and its doc comments | hand-written `getopt` |
| `Serialize` (serde) | conversion to JSON | none |

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Driver {
```

[tagprobe lib.rs:123](../../libraries/tagprobe-rs/src/lib.rs#L123).
Explained in: [3, Discovery](03-usb-and-stlink.md#discovery-usbdev-and-available);
[7, Choosing the driver](07-programming-tags-and-bases.md#choosing-the-driver-from-the-bases-identity);
[9, The common options](09-command-line-tools.md#the-common-options).

### `Default` by hand, and struct update syntax `..Default::default()`

When the derived zeros are wrong, `impl Default` is written out.
`Type { a: 1, ..Default::default() }` builds a struct from the named fields
plus the defaults for the rest: the nearest C++ is designated initialisers
with default member initialisers.

```rust
impl Default for TagMonitorStats {
    fn default() -> Self {
        Self {
            enabled: INSTRUMENTED,
```
```rust
let mut xo = ExternalCaptureOptions {
    prefix: "external_".into(),
    full,
    progress: Some(&mut forward),
    ..Default::default()
```

[monitor/mod.rs:159](../../libraries/tagcore-rs/src/monitor/mod.rs#L159),
[capture.rs:631](../../libraries/tagcore-rs/src/recovery/capture.rs#L631).
Explained in: [5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter);
[6, capture.rs](06-recovery.md#capturers-the-order-and-the-options).

### Field-init shorthand

`Struct { config, info }` means `Struct { config: config, info: info }`.
The `full` in the example above is the same thing.

```rust
Ok(LogHeader {
    config,
    info,
    calibration,
    states,
})
```

[tag-dwnld.rs:241](../../commandline-rs/src/bin/tag-dwnld.rs#L241).
Explained in: [9, The header](09-command-line-tools.md#the-header).

## Traits in use

The idea is in [the guide](../guide/14-rust-for-this-codebase.md#traits-interfaces-without-inheritance).

### A trait's methods need the trait in scope

A method that comes from a trait can be called only where the trait is
imported. A missing `use` shows up as "method not found", and a C++ reader
looking for where `emit` or `encode` is declared finds only the `use` line.
`use std::fmt::Write as _;` imports a trait for its methods without
binding its name.

```rust
use rusb::{Context, Device, DeviceDescriptor, UsbContext};
```
```rust
use tauri::{AppHandle, Emitter, Manager, State, WindowEvent, Wry};
```

[discovery.rs:16](../../libraries/tagcore-rs/src/link/discovery.rs#L16)
(for `ctx.devices()`),
[tagapp-shell lib.rs:41](../../libraries/tagapp-shell-rs/src/lib.rs#L41)
(for `handle.emit` and `tauri_app.manage`; Tag Monitor's `main.rs` instead
writes `tauri::Manager::path(setup.app)`, naming the trait at the call); `use prost::Message;` at
[rpc.rs:16](../../libraries/tagcore-rs/src/monitor/rpc.rs#L16);
`use std::fmt::Write as _;` at
[build.rs:14](../../libraries/tagcore-rs/build.rs#L14).
Explained in: [3, Discovery](03-usb-and-stlink.md#discovery-usbdev-and-available);
[4, The RPC](04-monitor-and-rpc.md#the-rpc);
[5, The embedded default configs](05-protobuf-and-the-tag-api.md#the-embedded-default-configs);
[10, main](10-tauri-shell.md#main).

### `Display` and `Debug`

`impl fmt::Display` is what `{}` and `.to_string()` print, the message for
a user; the derived `Debug` is what `{:?}` prints, the variant name.

```rust
impl fmt::Display for TargetFamily {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            TargetFamily::Stm32L4 => "STM32L4",
```

[monitor/mod.rs:208](../../libraries/tagcore-rs/src/monitor/mod.rs#L208).
Explained in: [4, The struct](04-monitor-and-rpc.md#the-struct);
[5, TagError](05-protobuf-and-the-tag-api.md#tagerror-an-error-written-by-hand).

### `Drop`

`impl Drop` is the destructor. It runs when the owner goes out of scope,
on an early `return` or `?`, and during a panic's unwinding; then each
field is dropped in declaration order. Assigning a new value to a field
drops the old one, so `self.handle = None;` closes a USB device.

```rust
impl Drop for LinkAdapt {
    fn drop(&mut self) {
        self.detach();
    }
}
```

[link/mod.rs:585](../../libraries/tagcore-rs/src/link/mod.rs#L585).
Explained in: [3, NO_DEVICE closes the probe](03-usb-and-stlink.md#no_device-closes-the-probe);
[3, Detach and Drop](03-usb-and-stlink.md#detach-and-drop);
[6, The type, and its destructor](06-recovery.md#the-type-and-its-destructor);
[8, Closing](08-bridges-to-cpp-and-python.md#closing).

### Implementing a library's trait

`impl Trait for MyType` makes `MyType` usable wherever the library expects
the trait, like deriving from its abstract class, except the
implementation is declared apart from the type. Methods with default bodies
need not be written: an empty `impl` takes them all.

```rust
impl log::Log for UiLogger {
    /// Passes records at or above the UI's level.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
```

[tagapp-engine lib.rs:485](../../libraries/tagapp-engine-rs/src/lib.rs#L485);
`impl serde_json::ser::Formatter for CppFloats` overriding two methods at
[proto.rs:79](../../libraries/tagcore-rs/src/proto.rs#L79); and the shared
engine toolkit's trait: `impl EngineState for Monitor`, which writes all
eight methods, at
[tagmonitor monitor.rs:849](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L849), and
`impl EngineState for Viewer` in Tag View, which
writes four of its eight methods and takes the defaults for the rest, at
[sensorviz-rs engine.rs:601](../../applications/sensorviz-rs/core/src/engine.rs#L601).

One limit, the *orphan rule*: `impl Trait for Type` may be written only in
the crate that defines the trait or the crate that defines the type. A
crate that owns neither cannot connect them, which is why the Tauri shell
takes the engine's two methods as function pointers rather than a trait
the applications would implement.
Explained in: [5, JSON](05-protobuf-and-the-tag-api.md#json-to_json-from_json-and-the-float-formatter);
[7, The session](07-programming-tags-and-bases.md#the-session-target-debug-sequence-attach-under-reset);
[10, The engine as the shell sees it](10-tauri-shell.md#the-engine-as-the-shell-sees-it) (the orphan rule);
[11, The logger](11-engine-and-model.md#the-logger).

### Trait objects: `dyn Trait` versus generics

`dyn Trait` behind a pointer (`Box<dyn T>`, `&mut dyn T`, `Arc<dyn T>`) is
a fat pointer of data and vtable: exactly a C++ pointer to an abstract
base, dispatched at run time. Generics and `impl Trait` are resolved at
compile time instead. The code uses `dyn` where types must be erased: a
stored callback, a probe handed to probe-rs, an optional progress closure.

```rust
) -> Result<Box<dyn ArmDebugInterface + 'probe>, (Box<dyn DebugProbe>, ArmError)> {
```

[fieldsafe.rs:275](../../libraries/tagprobe-rs/src/fieldsafe.rs#L275);
`Option<&mut dyn FnMut(&RescueProgress)>` at
[rescue.rs:150](../../libraries/tagcore-rs/src/recovery/rescue.rs#L150).
Explained in: [6, Core control and reads](06-recovery.md#core-control-and-reads);
[7, The probe](07-programming-tags-and-bases.md#the-probe);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn).

### Adding methods to a generated type

An `impl` block may add methods to any type defined in the same crate.
Because the prost output is `include!`d into `tagcore`, its types belong to
`tagcore`, and `proto.rs` can give `Ack` the accessor the C++ generated
code had.

```rust
impl Ack {
```

[proto.rs:35](../../libraries/tagcore-rs/src/proto.rs#L35).
Explained in: [5, proto.rs](05-protobuf-and-the-tag-api.md#protors-pulling-the-generated-code-in).

### `Send`, `Sync` and `'static` bounds

`Send` means a value may move to another thread, `Sync` that it may be
shared by reference between threads; the compiler derives both
automatically from the fields. A closure given to a thread must be `Send`
and `'static` (it owns its captures, borrowing nothing the spawning
function could free first).

```rust
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
```

[tagmonitor engine/mod.rs:104](../../applications/tagmonitor-rs/core/src/engine/mod.rs#L104).
Explained in: [8, The handle, `Send` and not `Sync`](08-bridges-to-cpp-and-python.md#the-handle-send-and-not-sync);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

## Collections and iterators

### `Vec`, `vec![]`, maps and the entry API

`Vec<T>` is `std::vector<T>`; `vec![0; n]` allocates `n` zeroes.
`HashMap` and `BTreeMap` are `unordered_map` and `map`.
`map.entry(k)` does one lookup and yields the slot: `.or_insert(v)` fills
it only if empty, `.or_default()` with the default, and both return a
`&mut` to what is there, replacing the C++ `find`-then-`insert`.

```rust
table.shapes.entry(key).or_default().paths.extend(paths);
table.defaults.entry(key).or_insert(config); // keeps the first
```

[configshape.rs:165](../../libraries/tagcore-rs/src/configshape.rs#L165);
`rpcbuf: vec![0; Self::RPCBUF_BYTES]` at
[monitor/mod.rs:353](../../libraries/tagcore-rs/src/monitor/mod.rs#L353).
Explained in: [4, The struct](04-monitor-and-rpc.md#the-struct);
[5, configshape.rs](05-protobuf-and-the-tag-api.md#configshapers-which-fields-a-tag-supports);
[7, Progress, and the flash loop](07-programming-tags-and-bases.md#progress-and-the-flash-loop).

### Chunks and windows

Slices split without index arithmetic: `chunks(n)` and `chunks_mut(n)`
yield consecutive pieces of at most `n` elements, `chunks_exact(n)` only
full pieces, `windows(n)` every overlapping run of `n` (a `memmem` in one
line), `as_chunks::<N>()` views a slice as `[T; N]` arrays.

```rust
for (i, chunk) in buf.chunks_mut(Self::MEM_CHUNK_BYTES).enumerate() {
```

[monitor/mod.rs:479](../../libraries/tagcore-rs/src/monitor/mod.rs#L479);
`image.windows(5).position(|w| w == b"base=")?` at
[base-flash.rs:156](../../commandline-rs/src/bin/base-flash.rs#L156);
`xyz.as_chunks::<3>().0` at
[calibsession lib.rs:302](../../libraries/calibsession-rs/src/lib.rs#L302).
Explained in: [4, Live memory access](04-monitor-and-rpc.md#live-memory-access);
[6, Core control and reads](06-recovery.md#core-control-and-reads);
[7, Memory through the base's own commands](07-programming-tags-and-bases.md#memory-through-the-bases-own-commands);
[7, base-flash](07-programming-tags-and-bases.md#base-flash-confirming-by-identity);
[8, calibsession](08-bridges-to-cpp-and-python.md#calibsession-the-same-pattern-more-types).

### Iterator chains

`iter()` borrows each element, `iter_mut()` mutably, `into_iter()` takes
them. Adapters (`map`, `filter`, `filter_map`, `find`, `find_map`,
`position`, `enumerate`, `zip`, `flatten`, `take`, `copied`) build a lazy
pipeline, like C++20 ranges; nothing runs until a consumer (`collect`,
`min`, `all`, `count`, a `for` loop) drains it. The code prefers them to
index loops.

```rust
[
    self.base_poll,
    self.status_poll,
    self.attach_retry.map(|(t, _)| t),
    self.session.as_ref().and_then(|s| s.tick_at),
]
.into_iter()
.flatten()
.min()
```

[station.rs:573](../../applications/tagprogram-rs/core/src/engine/station.rs#L573)
(the earliest armed timer, or `None`).
Explained in: [6, mcu.rs](06-recovery.md#mcurs-one-table-per-part);
[7, The connect check](07-programming-tags-and-bases.md#the-connect-check-without-probe-rs);
[7, Opening the probe](07-programming-tags-and-bases.md#opening-the-probe);
[14, The session step machine](14-the-three-applications.md#the-session-step-machine).

### Patterns in closure parameters: `|&b|`, `|(f, _)|`, `**c`

Closure parameters are patterns. `iter()` yields references, so
`|&b| b == 0` destructures a `&u8` into a `u8`, `|(f, _)|` takes a tuple
apart, and a closure given a `&&T` by `find` uses `**c` to reach the value.

```rust
.position(|&b| b == 0)
```
```rust
if let Some(&command) = ALL_COMMANDS.iter().find(|c| command_name(**c) == cmd) {
```

[monitor/mod.rs:414](../../libraries/tagcore-rs/src/monitor/mod.rs#L414),
[monitor.rs:481](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L481).
Explained in: [3, Base identify](03-usb-and-stlink.md#base-identify-f6-99);
[5, textformat.rs](05-protobuf-and-the-tag-api.md#textformatrs-debugstring-byte-for-byte);
[11, Commands and their availability](11-engine-and-model.md#commands-and-their-availability);
[13, Hop 3](13-tracing-start.md#hop-3-the-engine).

### `collect` builds the container the type asks for

`collect()` drains an iterator into whatever collection the context names:
a `Vec`, a `String` (from `char`s or `String`s), a JSON `Map` (from key and
value pairs). The type comes from the `let` annotation or a turbofish,
often `Vec<_>` with the element type inferred.

```rust
let mut fields: Vec<(FieldDescriptor, &Value)> = msg.fields().collect();
fields.sort_by_key(|(f, _)| f.number());
```

[textformat.rs:42](../../libraries/tagcore-rs/src/textformat.rs#L42).
Explained in: [5, textformat.rs](05-protobuf-and-the-tag-api.md#textformatrs-debugstring-byte-for-byte);
[7, Opening the probe](07-programming-tags-and-bases.md#opening-the-probe);
[11, The view state](11-engine-and-model.md#the-view-state);
[13, Hop 7](13-tracing-start.md#hop-7-the-view-state-and-the-redraw).

### A closure that returns an iterator

Because adapters are lazy values, a closure can return a fresh pipeline
each time it is called, to scan the same data twice.

```rust
let code_units = || (0..units).map(|i| (raw[2 + 2 * i], raw[3 + 2 * i]));

if code_units().all(|(lo, hi)| hi == 0 && (0x21..=0x7e).contains(&lo)) {
    return code_units().map(|(lo, _)| lo as char).collect();
}
```

[discovery.rs:78](../../libraries/tagcore-rs/src/link/discovery.rs#L78).
Not discussed in a chapter.

### `for` over an array of tuples

An array literal of tuples, destructured in the `for`, is a lookup table
without declaring a struct.

```rust
for (addr, family) in [
    (STM32L4_DBGMCU_IDCODE, TargetFamily::Stm32L4),
    (STM32U3_DBGMCU_IDCODE, TargetFamily::Stm32U3),
] {
```

[monitor/mod.rs:554](../../libraries/tagcore-rs/src/monitor/mod.rs#L554).
Explained in: [4, Attach](04-monitor-and-rpc.md#attach).

## Closures

### Closure syntax and captures

`|x| expr` or `|x| { ... }` is a lambda; parameter types are usually
inferred. It captures what it uses by shared reference, by mutable
reference if it modifies it, or by value if needed, and the borrow rules
apply to captures: while a closure holding `&mut self` is alive, nothing
else may use `self`.

```rust
let mut reg = |r| self.read_core_register(r).unwrap_or(0);
```

[cortex_m.rs:207](../../libraries/tagcore-rs/src/monitor/cortex_m.rs#L207).
Explained in: [3, Opening a probe](03-usb-and-stlink.md#opening-a-probe-and-choosing-the-endpoints);
[4, When the firmware does not answer](04-monitor-and-rpc.md#when-the-firmware-does-not-answer);
[7, Programming, and counting resets](07-programming-tags-and-bases.md#programming-and-counting-resets).

### `move` closures, and cloning before the move

`move |x| ...` takes ownership of every capture, like a C++ lambda
capturing by value with `std::move`. A closure that runs on another thread
or after the current function returns must be `move`. To give a thread its
own copy of an `Arc` or a `Sender` and keep the original, the code clones
it in a block under the same name first.

```rust
{
    let cancel = cancel.clone();
    std::thread::Builder::new()
        .name("tagmonitor-tag".into())
        .spawn(move || tagthread::run(tag_rx, poster, cancel))
        .expect("tag thread");
}
```

[tagmonitor engine/mod.rs:108](../../applications/tagmonitor-rs/core/src/engine/mod.rs#L108).
Explained in: [7, Writing](07-programming-tags-and-bases.md#writing-and-treating-the-departure-as-success);
[10, main](10-tauri-shell.md#main);
[11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn);
[14, The core](14-the-three-applications.md#the-core-tag-monitors-two-threads-over-a-c-session).

### `Fn`, `FnMut`, `FnOnce`

The three callable traits differ in how the callable may be called:
`Fn` through a shared reference (any number of times, concurrently),
`FnMut` through a mutable one (it may change its captured state; the
parameter must be `mut`), `FnOnce` by value, once (it may consume its
captures).

```rust
mut on_progress: impl FnMut(Progress),
```
```rust
apply: impl FnOnce(&mut Station, T) + Send + 'static,
```

[tagprobe lib.rs:484](../../libraries/tagprobe-rs/src/lib.rs#L484),
[station.rs:500](../../applications/tagprogram-rs/core/src/engine/station.rs#L500);
`F: FnOnce(Setup<'_>) -> Result<Engine, Box<dyn Error>> + Send + 'static`,
the application's setup closure, at
[tagapp-shell lib.rs:271](../../libraries/tagapp-shell-rs/src/lib.rs#L271).
Explained in: [7, The program sequence](07-programming-tags-and-bases.md#the-program-sequence);
[10, run: building the application](10-tauri-shell.md#run-building-the-application);
[11, Download](11-engine-and-model.md#download-two-functions-one-chosen-at-compile-time);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### Closures and functions as values

Every closure has its own unnamed type; `Box<dyn FnOnce(...) + Send>`
erases it so closures can travel in a channel message (a move-only
`std::function`). A function or method name is itself a value and can be
passed to `map`, like a function pointer.

```rust
pub(crate) type Done = Box<dyn FnOnce(&mut Station) + Send>;
```
```rust
.map(Probe::from_specific_probe)
```

[tagprogram engine/mod.rs:90](../../applications/tagprogram-rs/core/src/engine/mod.rs#L90),
[tagprobe lib.rs:357](../../libraries/tagprobe-rs/src/lib.rs#L357);
`Engine::new(engine, engine::Engine::command, engine::Engine::query)`, two
methods passed as `fn(&E, &str, Value)` pointers, at
[tagmonitor main.rs:52](../../applications/tagmonitor-rs/app/src/main.rs#L52).
Explained in: [7, Opening the probe](07-programming-tags-and-bases.md#opening-the-probe);
[9, The state log and a method as a function pointer](09-command-line-tools.md#the-state-log-and-a-method-as-a-function-pointer);
[10, The engine as the shell sees it](10-tauri-shell.md#the-engine-as-the-shell-sees-it);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

## Threads and shared state

The design is in [the guide](../guide/14-rust-for-this-codebase.md#threads-and-channels)
and [chapter 11](11-engine-and-model.md#three-threads-and-two-channels).

### `std::thread::spawn` and `thread::Builder`

`std::thread::spawn(closure)` starts a thread running the closure;
`thread::Builder` does the same with a name. The closure must be
`Send + 'static`. The applications never join their threads; the engine
loop, for one, ends when its channel reports `Disconnected`.

```rust
std::thread::spawn(move || {
```

[station.rs:503](../../applications/tagprogram-rs/core/src/engine/station.rs#L503).
Explained in: [11, The Engine handle and spawn](11-engine-and-model.md#the-engine-handle-and-spawn);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### Channels: `mpsc`, `send`, `recv_timeout`

`mpsc::channel()` returns a `(Sender, Receiver)` pair: a multi-producer,
single-consumer queue. `send` never blocks (the queue is unbounded) and
fails only if the receiver is gone. `recv_timeout(d)` waits at most `d`,
which is the engine loop's timer. A reply channel sent inside a message
turns a query into a synchronous call.

```rust
match inbox.rx.recv_timeout(wait) {
    Ok(Msg::Ui(Request::Command(cmd, args))) => state.command(&cmd, &args),
```

[tagapp-engine lib.rs:285](../../libraries/tagapp-engine-rs/src/lib.rs#L285), the engine loop Tag Monitor runs.
Explained in: [11, The engine loop](11-engine-and-model.md#the-engine-loop);
[13, Hop 2](13-tracing-start.md#hop-2-the-shell).

### `Arc` and atomics

`Arc<T>` is an atomically counted shared pointer, `std::shared_ptr`; it
gives shared (`const`) access only. `Arc<AtomicBool>` is the cancel and
exit flag; `Ordering::Relaxed` is `std::memory_order_relaxed`.

```rust
let exiting = Arc::new(AtomicBool::new(false));
let exiting_close = exiting.clone();
```

[tagapp-shell lib.rs:276](../../libraries/tagapp-shell-rs/src/lib.rs#L276).
Explained in: [10, run: building the application](10-tauri-shell.md#run-building-the-application);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### `Mutex`, guards and poisoning

`Mutex<T>` owns the data it protects: the only way in is the
`MutexGuard` that `lock()` returns, and the guard unlocks when dropped
(`std::lock_guard`, but the data cannot be reached without it). A
temporary guard lives to the end of its statement. If a thread panics
while holding the lock, later `lock()` calls return `Err` ("poisoned");
`unwrap_or_else(|e| e.into_inner())` takes the guard anyway.

```rust
self.inner.lock().unwrap_or_else(|e| e.into_inner())
```

[tag.rs:362](../../libraries/tagcore-rs/src/tag.rs#L362).
Explained in: [3, `&mut self` instead of a recursive mutex](03-usb-and-stlink.md#mut-self-instead-of-a-recursive-mutex);
[5, Tag: one mutex](05-protobuf-and-the-tag-api.md#tag-one-mutex-self-everywhere);
[13, Tag::start](13-tracing-start.md#tagstart);
[14, One engine thread](14-the-three-applications.md#one-engine-thread-that-owns-the-tag-and-workers-that-return-closures).

### Lazily initialised globals: `LazyLock`, `OnceLock`, `Once`

`LazyLock<T>` is a static whose initialiser closure runs once, on first
use from any thread: a C++ function-local `static`. `OnceLock<T>` is set
once at a time the program chooses (`get_or_init`). `Once` runs a block
once per process.

```rust
pub static DESCRIPTOR_POOL: LazyLock<DescriptorPool> = LazyLock::new(|| {
```
```rust
static LOG_SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
```

[proto.rs:28](../../libraries/tagcore-rs/src/proto.rs#L28),
[tagapp-engine lib.rs:468](../../libraries/tagapp-engine-rs/src/lib.rs#L468).
Explained in: [5, proto.rs](05-protobuf-and-the-tag-api.md#protors-pulling-the-generated-code-in);
[11, The logger](11-engine-and-model.md#the-logger).

### `thread_local!`

A per-thread static, C++ `thread_local`. The link tests use one so that
tests running on parallel threads each see only their own log records.

```rust
thread_local! {
    /// Levels of the records this test thread logged from this module.
    static RECORDS: RefCell<Vec<Level>> = const { RefCell::new(Vec::new()) };
}
```

[link/mod.rs:600](../../libraries/tagcore-rs/src/link/mod.rs#L600).
Explained in: [3, Failure runs](03-usb-and-stlink.md#failure-runs-one-error-per-burst).

## Modules and visibility

### Modules: `mod`, `mod.rs`, inline `mod`

There are no headers. `mod link;` makes the compiler read `link.rs` or
`link/mod.rs` as the module `crate::link`: the module tree is declared in
code. `pub mod status { ... }` declares a module inline, which the code
uses as a namespace for constants.

```rust
pub mod abi;
pub mod cortex_m;
pub mod l4;
```

[monitor/mod.rs:80](../../libraries/tagcore-rs/src/monitor/mod.rs#L80).
Explained in: [1, Where the files of a crate go](01-workspace-and-build.md#where-the-files-of-a-crate-go);
[3, Where the link sits](03-usb-and-stlink.md#where-the-link-sits);
[6, xflash.rs](06-recovery.md#xflashrs-the-serve-loader-protocol).

### Visibility: `pub`, `pub(crate)`, `pub(super)`

Items are private to their module by default. `pub` is public,
`pub(crate)` visible anywhere in this crate but not to its users,
`pub(super)` visible to the parent module only. Visibility belongs to the
item, not the file: a private module may define `pub` methods of a public
type.

```rust
pub(super) fn call_l4(&mut self, operation: u8, operand: i32) -> Option<u32> {
```

[l4.rs:110](../../libraries/tagcore-rs/src/monitor/l4.rs#L110);
`pub(crate) enum Report` at
[tagmonitor tagthread.rs:69](../../applications/tagmonitor-rs/core/src/engine/tagthread.rs#L69).
Explained in: [3, Where the link sits](03-usb-and-stlink.md#where-the-link-sits);
[3, The 16-byte command block](03-usb-and-stlink.md#the-16-byte-command-block);
[11, The messages](11-engine-and-model.md#the-messages);
[11, Monitor: the engine's state](11-engine-and-model.md#monitor-the-engines-state).

### `use` paths, aliases and re-exports

`use` brings names into scope; it includes nothing. Paths start at
`crate::` (this crate's root), `super::` (the parent module) or a crate
name. `use layout as l;` is a local alias. `pub use` re-exports a name so
callers can write `tagcore::Tag` instead of `tagcore::tag::Tag`, a `using`
declaration in a public header.

```rust
pub use link::{LinkAdapt, LinkAdaptStats, UsbDev};
pub use monitor::{BitTagFlashLog, MemAccess, TagMonitor, TagMonitorStats, TargetFamily};
pub use tag::{StopFailure, Tag, TagError};
```

[tagcore lib.rs:58](../../libraries/tagcore-rs/src/lib.rs#L58);
`use layout as l;` at
[xflash.rs:96](../../libraries/tagcore-rs/src/recovery/xflash.rs#L96).
Explained in: [3, Where the link sits](03-usb-and-stlink.md#where-the-link-sits);
[6, Two ways to reach a tag](06-recovery.md#two-ways-to-reach-a-tag);
[6, xflash.rs](06-recovery.md#xflashrs-the-serve-loader-protocol);
[7, The erase plan](07-programming-tags-and-bases.md#the-erase-plan).

### One type, many `impl` blocks

Any module of the crate may add an `impl Type { ... }` block; there is no
class body listing the members. `LinkAdapt`'s methods are in four files,
`TagMonitor`'s in six: the C++ habit of defining one class's members in
several `.cc` files, without the header.

```rust
impl TagMonitor {
```

[l4.rs:43](../../libraries/tagcore-rs/src/monitor/l4.rs#L43),
also [rescue.rs:119](../../libraries/tagcore-rs/src/monitor/rescue.rs#L119)
and [monitor/mod.rs:333](../../libraries/tagcore-rs/src/monitor/mod.rs#L333).
Explained in: [3, Where the link sits](03-usb-and-stlink.md#where-the-link-sits);
[11, Monitor: the engine's state](11-engine-and-model.md#monitor-the-engines-state);
[14, The Station](14-the-three-applications.md#the-station).

### Crate names: hyphens and underscores

A package name may contain hyphens (`tag-tools`, `tagcore-c-sys`); in Rust
source the crate is named with underscores (`use tag_tools::BaseArgs;`).
The hyphen form is what `cargo -p` takes.

Explained in: [1, Cargo's vocabulary](01-workspace-and-build.md#cargos-vocabulary-in-cmake-terms).

### Tests beside the code: `#[cfg(test)] mod tests`

Unit tests sit at the bottom of the file they test, in a child module
compiled only for `cargo test`. Being a child, it sees the file's private
items through `use super::*;`. `#[test]` marks a test; `#[ignore = "..."]`
one that runs only with `-- --ignored` (here, ones that need files or
hardware). Files in `tests/` are separate programs using the public API.

```rust
#[cfg(test)]
mod tests {
    use super::*;
```

[monitor/mod.rs:743](../../libraries/tagcore-rs/src/monitor/mod.rs#L743);
`#[ignore = ...]` at
[image.rs:433](../../applications/tagprogram-rs/core/src/model/image.rs#L433).
Explained in: [1, Tests](01-workspace-and-build.md#tests);
[11, Testing without Tauri or a tag](11-engine-and-model.md#testing-without-tauri-or-a-tag).

## Macros

A name followed by `!` is a macro, expanded at compile time. Those that
occur here:

| Macro | Does | Example |
| --- | --- | --- |
| `format!`, `println!`, `eprintln!` | Format to a `String` or stdout/stderr | [tag-dwnld.rs:152](../../commandline-rs/src/bin/tag-dwnld.rs#L152) |
| `write!`, `writeln!` | Format into a `String` (with `fmt::Write`) or a stream; returns a `Result` | [capture.rs:405](../../libraries/tagcore-rs/src/recovery/capture.rs#L405) |
| `format_args!` | A pre-parsed message (`fmt::Arguments`) that allocates nothing until formatted | [link/mod.rs:493](../../libraries/tagcore-rs/src/link/mod.rs#L493) |
| `error!`, `warn!`, `info!`, `debug!` | Log at a fixed level through the `log` facade; they evaluate to `()` | `debug!("{cmd}: not available now")`, [monitor.rs:485](../../applications/tagmonitor-rs/core/src/engine/monitor.rs#L485) |
| `log!(level, ...)` | Log at a level chosen at run time | [l4.rs:131](../../libraries/tagcore-rs/src/monitor/l4.rs#L131) |
| `vec![x; n]` | A `Vec` of `n` copies | [monitor/mod.rs:353](../../libraries/tagcore-rs/src/monitor/mod.rs#L353) |
| `json!({...})` | A `serde_json::Value` written in JSON syntax, with Rust expressions as values (`None` becomes `null`) | [view.rs:100](../../applications/tagmonitor-rs/core/src/engine/view.rs#L100) |
| `matches!` | Pattern test as a `bool`; see [`matches!`](#matches) | [monitor/mod.rs:706](../../libraries/tagcore-rs/src/monitor/mod.rs#L706) |
| `cfg!(...)` | A compile-time `true`/`false`; the code it guards is still compiled and type-checked (`if constexpr`) | [link/mod.rs:80](../../libraries/tagcore-rs/src/link/mod.rs#L80) |
| `include!`, `include_bytes!`, `include_str!` | Paste a file's Rust tokens, or embed its bytes or text, at compile time | [proto.rs:25](../../libraries/tagcore-rs/src/proto.rs#L25) |
| `env!`, `option_env!`, `concat!` | Read an environment variable at compile time (`option_env!` gives `None` if unset); join literals | [session.rs:81](../../applications/tagprogram-rs/core/src/engine/session.rs#L81) |
| `panic!`, `debug_assert!` | Abort the thread; a check that only debug builds run | [configshape.rs:253](../../libraries/tagcore-rs/src/configshape.rs#L253) |
| `thread_local!` | A per-thread static; see [`thread_local!`](#thread_local) | [link/mod.rs:600](../../libraries/tagcore-rs/src/link/mod.rs#L600) |
| `tauri::generate_handler![...]`, `tauri::generate_context!()` | The command dispatch table; the configuration and page embedded in the binary | [tagapp-shell lib.rs:314](../../libraries/tagapp-shell-rs/src/lib.rs#L314), [tagmonitor main.rs:37](../../applications/tagmonitor-rs/app/src/main.rs#L37) |

The `cfg!` constant, as the code writes it,
[link/mod.rs:80](../../libraries/tagcore-rs/src/link/mod.rs#L80):

```rust
const INSTRUMENTED: bool = cfg!(feature = "instrumentation");
```

Explained in: [3, Attach](03-usb-and-stlink.md#attach-claim-reset-enter-swd) (format arguments);
[3, Debug registers](03-usb-and-stlink.md#debug-registers) (`log!`);
[3, Failure runs](03-usb-and-stlink.md#failure-runs-one-error-per-burst) (`format_args!`);
[3, Instrumentation](03-usb-and-stlink.md#instrumentation) (`cfg!`);
[5, proto.rs](05-protobuf-and-the-tag-api.md#protors-pulling-the-generated-code-in) (`include!`, `env!`);
[11, Commands and their availability](11-engine-and-model.md#commands-and-their-availability) (`debug!`);
[11, The view state](11-engine-and-model.md#the-view-state) (`json!`).

## Attributes

`#[...]` applies to the next item; `#![...]` (with `!`) to the enclosing
module or crate.

| Attribute | Means | Example |
| --- | --- | --- |
| `#[derive(...)]` | Generate trait impls; see [`derive`](#derive) | [tagprobe lib.rs:123](../../libraries/tagprobe-rs/src/lib.rs#L123) |
| `#[cfg(...)]` | Compile the item only if the condition holds, `#ifdef`: `test`, `unix`, `target_os = "macos"`, `feature = "sqlitelog"` | [download.rs:78](../../applications/tagmonitor-rs/core/src/engine/download.rs#L78) |
| `#![cfg_attr(cond, attr)]` | Apply `attr` only when `cond` holds; here the Windows GUI subsystem in release builds | [tagmonitor main.rs:17](../../applications/tagmonitor-rs/app/src/main.rs#L17) |
| `#![allow(...)]` | Silence lints for the module, e.g. generated code without docs | [proto.rs:17](../../libraries/tagcore-rs/src/proto.rs#L17) |
| `#![no_std]` | The crate uses only `core`, no OS services | [tagcore-c-sys lib.rs:21](../../libraries/tagcore-c-sys/src/lib.rs#L21) |
| `#[repr(C)]` | C layout for a struct that crosses to C | [tlw.rs:10](../../libraries/tagcore-c-sys/src/tlw.rs#L10) |
| `#[default]` | The default enum variant; see [`#[default]`](#default-on-a-variant) | [monitor/mod.rs:184](../../libraries/tagcore-rs/src/monitor/mod.rs#L184) |
| `#[test]`, `#[ignore = "..."]` | A unit test; one skipped unless asked for | [image.rs:433](../../applications/tagprogram-rs/core/src/model/image.rs#L433) |
| `#[arg(...)]`, `#[command(...)]` (clap) | Per-option and per-command settings for the derived parser | [tag-flash.rs:106](../../commandline-rs/src/bin/tag-flash.rs#L106) |
| `#[tauri::command]` | Generate the glue that lets the page `invoke` the function by name, as `moc` does for `Q_INVOKABLE` | [tagapp-shell lib.rs:152](../../libraries/tagapp-shell-rs/src/lib.rs#L152) |
| `#[pyclass]`, `#[pymethods]`, `#[pyfunction]` | Expose a struct, its methods or a function to Python | [tagcore-py lib.rs:180](../../python/tagcore-py/src/lib.rs#L180) |

Doc comments are attributes too: `///` documents the next item and `//!`
the enclosing file or crate. clap turns a field's `///` comment into its
`--help` text.

Explained in: [5, proto.rs](05-protobuf-and-the-tag-api.md#protors-pulling-the-generated-code-in) (`#![allow]`);
[8, One crate may link the library](08-bridges-to-cpp-and-python.md#one-crate-may-link-the-library) (`#![no_std]`);
[9, Ctrl-C and SIGPIPE](09-command-line-tools.md#ctrl-c-and-sigpipe) (`#[cfg(unix)]`);
[10, The crate attribute and the imports](10-tauri-shell.md#the-crate-attribute-and-the-imports) (`cfg_attr`);
[10, The two commands](10-tauri-shell.md#the-two-commands) (`#[tauri::command]`);
[10, The user guide and App Nap](10-tauri-shell.md#the-user-guide-and-app-nap) (`#[cfg(target_os)]`);
[10, The macOS application menu](10-tauri-shell.md#the-macos-application-menu) (`#[cfg(target_os)]` on items);
[11, Download](11-engine-and-model.md#download-two-functions-one-chosen-at-compile-time) (`#[cfg(feature)]`).

## Build time: build scripts and features

### `build.rs` and its `cargo:` lines

A package's `build.rs` is a Rust program Cargo compiles and runs before
the crate, in the role of a CMake custom command. It talks to Cargo by
printing lines: `cargo:rerun-if-changed=PATH` (the `DEPENDS` list),
`cargo:rustc-env=NAME=VALUE` (readable with `env!`),
`cargo:rustc-link-lib` and `cargo:rustc-link-search` (link directives),
`cargo:rustc-link-arg-bins` (a linker argument for this package's binaries).
Cargo gives it `OUT_DIR`, a private output directory, and
`CARGO_FEATURE_*` and `CARGO_CFG_*` variables.

```rust
println!("cargo:rerun-if-changed={}", tag_proto.display());
```

[tagcore build.rs:28](../../libraries/tagcore-rs/build.rs#L28).
Explained in: [1, Build scripts](01-workspace-and-build.md#build-scripts);
[5, From `.proto` to Rust at build time](05-protobuf-and-the-tag-api.md#from-proto-to-rust-at-build-time);
[8, The build script](08-bridges-to-cpp-and-python.md#the-build-script-link-directives);
[10, build.rs](10-tauri-shell.md#buildrs-staging-the-page).

### Generated code: `OUT_DIR` and `include!`

A build script writes generated Rust into `OUT_DIR`; the crate pulls it in
with `include!(concat!(env!("OUT_DIR"), "/file.rs"))`, so the generated
items become items of that module. This is the one place Rust does a
C-style include.

```rust
include!(concat!(env!("OUT_DIR"), "/_.rs"));
```

[proto.rs:25](../../libraries/tagcore-rs/src/proto.rs#L25).
Explained in: [1, Code generation](01-workspace-and-build.md#code-generation-tagcore);
[5, proto.rs](05-protobuf-and-the-tag-api.md#protors-pulling-the-generated-code-in).

### The target in a build script: `CARGO_CFG_TARGET_OS`

A build script runs on the build machine, so `cfg!` and `#[cfg]` inside it
describe the host. The target being built is in the `CARGO_CFG_*`
variables.

```rust
if env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
```

[commandline-rs build.rs:30](../../commandline-rs/build.rs#L30).
Explained in: [1, Linking a CMake product](01-workspace-and-build.md#linking-a-cmake-product-tagcore-c-sys-and-links);
[8, The rpath](08-bridges-to-cpp-and-python.md#the-rpath-only-the-final-binary-can-set-it).

### Features, optional and target-specific dependencies

A feature is a named compile-time switch declared in `Cargo.toml`, a `-D`
option the build system understands; code tests it with
`#[cfg(feature = "...")]`. `optional = true` plus `"dep:name"` in a feature
makes a dependency exist only when the feature is on. `required-features`
skips a binary without it. A `[target.'cfg(...)'.dependencies]` table is a
dependency for one platform only; `[build-dependencies]` are for
`build.rs`.

```toml
sqlitelog = ["dep:taglogwriter", "dep:tagcore-c-sys"]
```

[commandline-rs Cargo.toml:36](../../commandline-rs/Cargo.toml#L36);
`[target.'cfg(target_os = "macos")'.dependencies]` at
[tagapp-shell Cargo.toml:18](../../libraries/tagapp-shell-rs/Cargo.toml#L18).
Explained in: [1, Reading a package manifest](01-workspace-and-build.md#reading-a-package-manifest);
[9, Building it](09-command-line-tools.md#building-it);
[10, The crate manifest](10-tauri-shell.md#the-crate-manifest);
[10, tagapp-shell](10-tauri-shell.md#tagapp-shell-the-shared-shell);
[11, The crate](11-engine-and-model.md#the-crate).

### `links` and `DEP_*` variables

`links = "tagcore_c"` in a manifest declares that this package links a
native library, and only one package in a build may do so. What its build
script prints as `cargo:KEY=VALUE` reaches the build scripts of direct
dependents as `DEP_TAGCORE_C_KEY`.

```rust
let Ok(lib_dir) = env::var("DEP_TAGCORE_C_LIB_DIR") else {
    return;
};
```

[commandline-rs build.rs:27](../../commandline-rs/build.rs#L27);
`links` at [tagcore-c-sys Cargo.toml:17](../../libraries/tagcore-c-sys/Cargo.toml#L17).
Explained in: [1, Linking a CMake product](01-workspace-and-build.md#linking-a-cmake-product-tagcore-c-sys-and-links);
[8, One crate may link the library](08-bridges-to-cpp-and-python.md#one-crate-may-link-the-library).

## `unsafe` and FFI

`unsafe` code is confined to the bridge crates, `tagcore`'s build script
and the SIGPIPE reset in `tag-tools`' library; the rules the wrappers keep are in [chapter 8](08-bridges-to-cpp-and-python.md#what-unsafe-means-precisely)
and [the guide](../guide/14-rust-for-this-codebase.md#foreign-functions).

### `unsafe extern "C"` blocks

Declares C functions, like a header's prototypes. The declarations are
unchecked claims about the library (a wrong parameter type is not a
compile error anywhere); edition 2024 requires `unsafe` on the block to
say so. Each function in it is `unsafe` to call.

```rust
unsafe extern "C" {
    /// The interface version the library was built with.
    pub fn tlw_abi_version() -> u32;
```

[tlw.rs:27](../../libraries/tagcore-c-sys/src/tlw.rs#L27).
Explained in: [8, Declaring C in Rust](08-bridges-to-cpp-and-python.md#declaring-c-in-rust).

### `unsafe { }` and `// SAFETY:`

An `unsafe` block marks code whose soundness the compiler cannot check:
calling a foreign or `unsafe fn`, dereferencing a raw pointer. The tree's
rule is a `// SAFETY:` comment above each block saying why it is sound.
In edition 2024 `std::env::set_var` is an `unsafe fn` too, because another
thread reading the environment at the same time is undefined behaviour.

```rust
// SAFETY: build scripts are single-threaded.
unsafe { std::env::set_var("PROTOC", protoc) };
```

[tagcore build.rs:38](../../libraries/tagcore-rs/build.rs#L38).
Explained in: [5, From `.proto` to Rust](05-protobuf-and-the-tag-api.md#from-proto-to-rust-at-build-time);
[8, What `unsafe` means, precisely](08-bridges-to-cpp-and-python.md#what-unsafe-means-precisely);
[9, Ctrl-C and SIGPIPE](09-command-line-tools.md#ctrl-c-and-sigpipe).

### Opaque handles and `#[repr(C)]`

A struct whose only field is a private zero-length array cannot be built
outside its module, so Rust code can hold a `*mut TlwWriter` but never a
`TlwWriter`: the C incomplete type. `#[repr(C)]` gives a struct C's field
order and padding (Rust's own layout may reorder fields).

```rust
#[repr(C)]
pub struct TlwWriter {
    /// Makes the type unconstructible and unsized-like in Rust.
    _private: [u8; 0],
}
```

[tlw.rs:10](../../libraries/tagcore-c-sys/src/tlw.rs#L10).
Explained in: [8, Declaring C in Rust](08-bridges-to-cpp-and-python.md#declaring-c-in-rust).

### `NonNull<T>`

A raw pointer known not to be null. `NonNull::new(p)` returns an `Option`,
so the null check happens once, at construction.

```rust
let raw = NonNull::new(raw).ok_or_else(|| WriterError("Out of memory".into()))?;
```

[taglogwriter lib.rs:202](../../libraries/taglogwriter-rs/src/lib.rs#L202).
Explained in: [8, The handle, `Send` and not `Sync`](08-bridges-to-cpp-and-python.md#the-handle-send-and-not-sync).

### `CString` and `CStr`

`CString::new(s)` copies a string into an owned NUL-terminated buffer and
fails on an interior NUL; `.as_ptr()` lends it as `*const c_char` while
the `CString` lives (bind it to a local, or the pointer dangles).
`CStr::from_ptr(p)` views a C string (`strlen` plus `string_view`);
`to_string_lossy().into_owned()` copies it out before the C side can
reuse the buffer.

```rust
let text = unsafe { CStr::from_ptr(ffi::tlw_last_error(self.raw.as_ptr())) };
```

[taglogwriter lib.rs:327](../../libraries/taglogwriter-rs/src/lib.rs#L327);
`CString::new(path)` at
[taglogwriter lib.rs:188](../../libraries/taglogwriter-rs/src/lib.rs#L188).
Explained in: [8, Creating it](08-bridges-to-cpp-and-python.md#creating-it);
[8, Returned strings are copied at once](08-bridges-to-cpp-and-python.md#returned-strings-are-copied-at-once).

### References become raw pointers

A `&mut x` passed where a `*mut T` is expected converts implicitly; the
reverse never does. `.as_mut_ptr()` and `.len()` give a buffer's pointer
and length for C.

```rust
rbd::rbd_rebuild(dir.as_ptr(), db.as_ptr(), &mut records, error.as_mut_ptr(), error.len())
```

[taglogwriter lib.rs:390](../../libraries/taglogwriter-rs/src/lib.rs#L390).
Explained in: [8, A handle-less call](08-bridges-to-cpp-and-python.md#a-handle-less-call-with-a-callers-buffer).

### `unsafe impl Send`

A raw pointer makes a type neither `Send` nor `Sync`. When the C side
allows a handle to move between threads, the wrapper opts back in to
`Send` only, with the reason in a SAFETY comment; leaving out `Sync`
encodes "one thread at a time".

```rust
unsafe impl Send for LogWriter {}
```

[taglogwriter lib.rs:165](../../libraries/taglogwriter-rs/src/lib.rs#L165).
Explained in: [8, The handle, `Send` and not `Sync`](08-bridges-to-cpp-and-python.md#the-handle-send-and-not-sync).

## PyO3's types

Used only in [tagcore-py](../../python/tagcore-py/src/lib.rs); see
[chapter 8](08-bridges-to-cpp-and-python.md#the-python-bridge-tagcore-py).

### `Python<'py>`, `Bound<'py, T>`, `Py<T>`

`Python<'py>` is a zero-sized token that exists only while the thread is
attached to the interpreter (holds the GIL); functions that touch Python
objects take it as proof, and PyO3 supplies it. `Bound<'py, T>` is a
Python reference usable under that token; `Py<T>` is an owned, counted
reference that can be stored in a struct and turned into a `Bound` with
`.bind(py)`.

```rust
fn identity_dict<'py>(py: Python<'py>, r: &IdentityRecord) -> PyResult<Bound<'py, PyDict>> {
```

[tagcore-py lib.rs:117](../../python/tagcore-py/src/lib.rs#L117).
Explained in: [8, A function with defaults](08-bridges-to-cpp-and-python.md#a-function-with-defaults);
[8, A class that holds another](08-bridges-to-cpp-and-python.md#a-class-that-holds-another).

### `py.detach`: releasing the GIL

`py.detach(closure)` runs the closure with the GIL released, so other
Python threads run during a slow SWD call. The closure must be `Send`,
which is how PyO3 proves it holds nothing tied to the GIL.

```rust
if py.detach(|| s.write(addr, &data)) {
```

[tagcore-py lib.rs:266](../../python/tagcore-py/src/lib.rs#L266).
Explained in: [8, Releasing the GIL](08-bridges-to-cpp-and-python.md#releasing-the-gil).

### Higher-ranked bounds: `for<'py>`

`for<'py> Trait<'py>` requires the trait for every lifetime, because the
token it will be used with is created later, inside the function.

```rust
A: for<'py> pyo3::call::PyCallArgs<'py>,
```

[tagcore-py lib.rs:155](../../python/tagcore-py/src/lib.rs#L155).
Explained in: [8, Calling back into Python](08-bridges-to-cpp-and-python.md#calling-back-into-python-from-detached-code).

## `async`, in the two places it appears

The rest of the tree is synchronous and uses threads.

### `async fn`, `.await` and `block_on`

An `async fn` does not run when called; it returns a future, a state
machine that advances only when an executor polls it, and `.await` is
where it may suspend. `runtime.block_on(f)` polls `f` on this thread until
it finishes: the bridge back to ordinary code, used for the two `nusb`
calls in DFU.

```rust
let label = runtime.block_on(alt_string(&usb, &interface, FLASH_ALT))?;
```

[dfu.rs:205](../../libraries/tagprobe-rs/src/dfu.rs#L205).
Explained in: [7, Opening, and the two async steps](07-programming-tags-and-bases.md#opening-and-the-two-async-steps).

### `spawn_blocking` in a Tauri command

A Tauri command may be `async`. `spawn_blocking(closure)` runs ordinary
blocking code on a separate thread and gives a future of its result, so a
query that waits on the engine does not block the window's thread
(roughly `QtConcurrent::run` plus a continuation).

```rust
tauri::async_runtime::spawn_blocking(move || query(&name, args))
    .await
    .map_err(|e| e.to_string())
```

[tagapp-shell lib.rs:167](../../libraries/tagapp-shell-rs/src/lib.rs#L167).
Explained in: [10, The two commands](10-tauri-shell.md#the-two-commands).

## JavaScript

The pages are plain JavaScript with no build step: classic scripts, Preact
and htm. Only what the pages use is listed. [Chapter
12](12-ui-kit-and-pages.md) reads them.

### Classic scripts and one global per file

Each page loads its scripts with `<script src>` tags; the order of the
tags is the link order, and the scripts share one global object, `window`.
Each file exposes what it defines through a global (`kit.js` assigns
`window.Kit`, `kit-components.js` adds to it with `Object.assign`), and
later files read it. There are no `import` statements.

```html
<script src="../../../ui-kit/preact-htm.js"></script>
<script src="../../../ui-kit/kit.js"></script>
<script src="../../../ui-kit/kit-components.js"></script>
```

[tagmonitor index.html:30](../../applications/tagmonitor-rs/ui/index.html#L30);
`window.Kit = {` at [kit.js:560](../../ui-kit/kit.js#L560).
Explained in: [12, Classic scripts, loaded in a fixed order](12-ui-kit-and-pages.md#classic-scripts-loaded-in-a-fixed-order).

### IIFE and `"use strict"`

`(function () { ... })();` defines a function and calls it at once. Its
`const`s and functions are private to it, like an anonymous namespace in a
`.cc` file. `"use strict"` turns silent mistakes, such as assigning to an
undeclared name, into exceptions.

```js
(function () {
  "use strict";
```

[kit.js:16](../../ui-kit/kit.js#L16).
Explained in: [12, Classic scripts](12-ui-kit-and-pages.md#classic-scripts-loaded-in-a-fixed-order).

### `const`, `let` and arrow functions

`const` declares a name that is not reassigned (the object it names can
still change), `let` one that is. `(a, b) => expr` is a function returning
`expr`; `(a) => { ... }` has a body. The pages write most small functions
this way.

```js
const cx = (...names) => names.filter(Boolean).join(" ");
```

[kit-components.js:34](../../ui-kit/kit-components.js#L34).
Explained in: [12, Kit.createEngine](12-ui-kit-and-pages.md#kitcreateengine-tauri-or-the-mock).

### Closures instead of classes

A JavaScript function keeps the variables of the scope it was created in
alive for as long as it exists (a closure; captures behave like `[&]`
whose referents never die). The pages use this instead of classes: a
factory returns an object whose methods share private locals.

```js
function logModel() {
  let text = "";
  const subscribers = new Set();
  return {
    append(line) {
```

[kit-components.js:166](../../ui-kit/kit-components.js#L166).
Explained in: [12, The log](12-ui-kit-and-pages.md#the-log-a-model-outside-preact-a-pane-that-writes-the-dom-itself);
[14, The page](14-the-three-applications.md#the-page).

### Object literals and shorthand properties

`{ cmd, args: x }` builds an object; `{ cmd }` is short for
`{ cmd: cmd }`. An object literal indexed at once is a lookup table in
place of a `switch`; a missing key gives `undefined`.

```js
command: (cmd, args) => t.core.invoke("command", { cmd, args: args || {} }),
```
```js
const cmd = { o: "open", s: "save" }[key];
```

[kit.js:56](../../ui-kit/kit.js#L56),
[tagmonitor app.js:518](../../applications/tagmonitor-rs/ui/app.js#L518).
Explained in: [12, Kit.createEngine](12-ui-kit-and-pages.md#kitcreateengine-tauri-or-the-mock);
[12, Menus come from the engine](12-ui-kit-and-pages.md#menus-come-from-the-engine).

### Destructuring

`const { a, b } = obj` declares locals from the properties of the same
names (a structured binding by name); `class: cls` renames one.
`const [x, y] = arr` unpacks by position. Both work in parameter lists,
which is how a component takes its props.

```js
const { html, render, Fragment } = Kit;
```
```js
${TABS.map(([page, label, tooltip]) => html`<button type="button" role="tab" title=${tooltip}
```

[tagmonitor app.js:25](../../applications/tagmonitor-rs/ui/app.js#L25),
[tagmonitor app.js:91](../../applications/tagmonitor-rs/ui/app.js#L91).
Explained in: [12, kit-components.js](12-ui-kit-and-pages.md#kit-componentsjs-preact-for-a-qt-programmer);
[12, Components are functions of props](12-ui-kit-and-pages.md#components-are-functions-of-props);
[12, draw and Window](12-ui-kit-and-pages.md#draw-and-window);
[14, The page](14-the-three-applications.md#the-page).

### Rest and spread: `...`

In a parameter list or a destructuring, `...rest` collects the remaining
arguments into an array, or the remaining properties into an object. In an
array, a call or an htm tag, `...x` spreads them back out. Kit components
take what they use and pass the rest to the DOM element.

```js
function Button({ kind, small, class: cls, children, ...rest }) {
  return html`<button type="button" class=${cx("kit-button", kind, small && "small", cls)} ...${rest}>${children}</button>`;
}
```

[kit-components.js:47](../../ui-kit/kit-components.js#L47);
`[...byGroup]` spreads a `Map` into pairs at
[tagmonitor app.js:250](../../applications/tagmonitor-rs/ui/app.js#L250).
Explained in: [12, Kit.el and Kit.setupPage](12-ui-kit-and-pages.md#kitel-and-kitsetuppage);
[12, Components are functions of props](12-ui-kit-and-pages.md#components-are-functions-of-props);
[12, The configuration form](12-ui-kit-and-pages.md#the-configuration-form-is-generated-from-data).

### Default parameters

`= value` after a parameter supplies it when the caller omits it; with a
destructured parameter, `= {}` lets the caller pass nothing at all.

```js
async function openFile({ title, filters, defaultPath } = {}) {
```

[kit.js:503](../../ui-kit/kit.js#L503).
Explained in: [12, Files and URLs](12-ui-kit-and-pages.md#files-and-urls).

### Template literals and tagged templates (htm)

Backquoted strings interpolate `${expr}`, like `std::format`. A function
name placed before one makes a tagged template: the function receives the
literal pieces and the values separately. htm's `html` tag parses the
pieces as HTML-like markup (once per call site) and returns Preact virtual
nodes; values are never parsed as HTML, so there is no injection.
`<${Button} ...>` uses a component as the tag, and `<//>` closes it: JSX
without a compiler.

```js
${["start", "stop"].map((cmd) => html`<${Button} small hidden=${!actions[cmd].visible}
  disabled=${!actions[cmd].enabled} onClick=${() => send(cmd)}>${cmd === "start" ? "Start" : "Stop"}<//>`)}
```

[tagmonitor app.js:145](../../applications/tagmonitor-rs/ui/app.js#L145);
a template literal inside an htm value at
[heading-dial.js:52](../../ui-kit/heading-dial.js#L52).
Explained in: [12, Templates: the `html` tag](12-ui-kit-and-pages.md#templates-the-html-tag);
[12, heading-dial.js](12-ui-kit-and-pages.md#heading-dialjs);
[13, Hop 1](13-tracing-start.md#hop-1-the-page).

### Truthiness and the logical operators

`false`, `0`, `""`, `null`, `undefined` and `NaN` are falsy. `a && b`
yields `a` if falsy, else `b` (a compact `if` in a template); `a || b`
yields `a` if truthy, else `b` (a default); `a ?? b` replaces only `null`
or `undefined`, so `0` survives. `!!x` makes a real boolean. `===`
compares without type conversion; the pages never use `==`. Optional
chaining (`a?.b`) does not appear; the pages spell out `a && a.b`.

```js
}[name] ?? null)),
```

[tagmonitor mock-engine.js:236](../../applications/tagmonitor-rs/ui/mock-engine.js#L236);
`args || {}` at [kit.js:56](../../ui-kit/kit.js#L56);
`disabled=${!!o.disabled}` at
[kit-components.js:89](../../ui-kit/kit-components.js#L89).
Explained in: [12, Components are functions of props](12-ui-kit-and-pages.md#components-are-functions-of-props);
[12, Hooks](12-ui-kit-and-pages.md#hooks-four-of-them-used-sparingly);
[12, The mock engine](12-ui-kit-and-pages.md#the-mock-engine-and-the-preview-parameters);
[13, The StartAnyway prompt round trip](13-tracing-start.md#the-startanyway-prompt-round-trip).

### `null`, `undefined` and `NaN`

Two "no value"s: `undefined` is what a missing property or argument
gives, `null` what code sets on purpose; Preact and `Kit.el` skip an
attribute that is `undefined`. `NaN` (from `parseFloat("abc")`) is not
equal to itself, so it is tested with `Number.isNaN`.

```js
if (!Number.isNaN(v)) edit(k.id, v);
```

[tagmonitor app.js:317](../../applications/tagmonitor-rs/ui/app.js#L317).
Explained in: [12, Number and date fields keep their focus](12-ui-kit-and-pages.md#number-and-date-fields-keep-their-focus).

### Promises

A `Promise` is a future: an object that later holds a value or an error.
`.then(onOk, onErr)` registers what to do next and returns a new promise,
so steps chain; `Promise.all([...])` waits for several;
`Promise.resolve(x)` wraps a value that may or may not already be a
promise (the mock engine returns plain values, Tauri promises).
`new Promise((resolve) => { finish = resolve; })` keeps the settle
function for later: the `std::promise` kept while the `std::future` is
handed out.

```js
const promise = new Promise((resolve) => { finish = resolve; });
```

[kit.js:313](../../ui-kit/kit.js#L313);
`Promise.all([` at
[tagmonitor app.js:523](../../applications/tagmonitor-rs/ui/app.js#L523).
Explained in: [12, Kit.dialog](12-ui-kit-and-pages.md#kitdialog-one-modal-primitive-returning-a-promise);
[12, Boot](12-ui-kit-and-pages.md#boot);
[13, Hop 1](13-tracing-start.md#hop-1-the-page).

### `async` and `await`

An `async function` always returns a promise; inside it, `await p`
suspends that function only until `p` settles. The event loop keeps
running, so code that reads like a blocking `QDialog::exec()` leaves the
window live: state events still arrive and redraw it.

```js
async function showPrompt(p) {
  // ...
      await Kit.alert(p.title, p.text, info || null, p.style === "info" ? "info" : "warning");
```

[tagmonitor app.js:375](../../applications/tagmonitor-rs/ui/app.js#L375).
Explained in: [12, Kit.dialog](12-ui-kit-and-pages.md#kitdialog-one-modal-primitive-returning-a-promise);
[12, Files and URLs](12-ui-kit-and-pages.md#files-and-urls);
[13, The StartAnyway prompt round trip](13-tracing-start.md#the-startanyway-prompt-round-trip).

### `try` / `finally`

`finally` runs however the `try` block ends: normally, by `return`, or by
an exception, including a rejected `await`. Tag Programmer uses it to
clear a busy flag.

```js
try {
  const pv = await engine.query("program_preview");
  // ...
} finally {
  busy = false;
}
```

[tagprogram app.js:383](../../applications/tagprogram-rs/ui/app.js#L383).
Not discussed in a chapter.

### `Map` and `Set`

`Map` is a hash map that keeps insertion order (unlike `std::map`, which
sorts); `Set` a hash set. Spreading a `Map` (`[...m]`) gives an array of
`[key, value]` pairs.

```js
const byGroup = new Map();
```

[tagmonitor app.js:245](../../applications/tagmonitor-rs/ui/app.js#L245).
Explained in: [12, The configuration form](12-ui-kit-and-pages.md#the-configuration-form-is-generated-from-data).

### Preact hooks: `useState`, `useRef`, `useEffect`

Hooks give a function component memory between renders. `useState(v)`
returns `[value, setValue]`; calling the setter re-renders. `useRef(v)`
returns a box whose `.current` persists without re-rendering (an element,
or a counter). `useEffect(f, deps)` runs `f` after rendering when `deps`
change, and runs the function `f` returns to clean up.

```js
const [label, setLabel] = useState(initialLabel);
const [notes, setNotes] = useState("");
const seq = useRef(0);
```

[tagprogram app.js:342](../../applications/tagprogram-rs/ui/app.js#L342).
Explained in: [12, Hooks: four of them, used sparingly](12-ui-kit-and-pages.md#hooks-four-of-them-used-sparingly);
[14, The page](14-the-three-applications.md#the-page).

## Index

| Construct | Entry |
| --- | --- |
| `!` (bitwise NOT) | [Bitwise NOT is `!`, and ranges](#bitwise-not-is--and-ranges) |
| `!!`, `&&`, `\|\|`, `??`, `===` (JS) | [Truthiness](#truthiness-and-the-logical-operators) |
| `#![...]` inner attributes | [Attributes](#attributes) |
| `#[cfg]`, `#[cfg_attr]` | [Attributes](#attributes) |
| `#[default]` | [`#[default]` on a variant](#default-on-a-variant) |
| `#[derive]` | [`#[derive(...)]`](#derive) |
| `#[repr(C)]` | [Opaque handles and `#[repr(C)]`](#opaque-handles-and-reprc) |
| `#[tauri::command]` | [Attributes](#attributes) |
| `#[test]`, `#[ignore]`, `#[cfg(test)]` | [Tests beside the code](#tests-beside-the-code-cfgtest-mod-tests) |
| `&`, `&mut`, `&self`, `&mut self` | [References](#references-t-mut-t-self-mut-self) |
| `&*`, `*` on a `Box` | [`Box<T>`, `*` and `&*`](#boxt--and--and-deref-coercion) |
| `'static`, `'_`, `'a` | [Lifetimes](#lifetimes-static-_-a) |
| `..` in patterns | [Nested patterns](#nested-patterns--and-matching-through-a-reference) |
| `..Default::default()` | [`Default` by hand](#default-by-hand-and-struct-update-syntax-defaultdefault) |
| `...` rest and spread (JS) | [Rest and spread](#rest-and-spread-) |
| `::<T>` turbofish | [The turbofish](#the-turbofish-t) |
| `?` on `Option` | [`?` on an `Option`](#-on-an-option) |
| `?` on `Result` | [`?` on a `Result`](#-on-a-result-and-the-from-conversion) |
| `?` in a closure | [`?` inside a closure](#-inside-a-closure) |
| `?Sized` | [`?Sized`](#sized) |
| `a..b`, `a..=b` | [Bitwise NOT is `!`, and ranges](#bitwise-not-is--and-ranges) |
| `and_then`, `or_else`, `filter`, `map` | [Option and Result combinators](#option-and-result-combinators) |
| Arrow functions (JS) | [`const`, `let` and arrow functions](#const-let-and-arrow-functions) |
| `Arc` | [`Arc` and atomics](#arc-and-atomics) |
| `as` casts | [No implicit integer conversions](#no-implicit-integer-conversions-from-try_from-as) |
| `as_chunks`, `chunks`, `chunks_mut`, `windows` | [Chunks and windows](#chunks-and-windows) |
| `as_deref`, `as_deref_mut`, `as_ref` | [`as_ref`, `as_deref`, `as_deref_mut`](#as_ref-as_deref-as_deref_mut) |
| `async fn`, `.await`, `block_on` | [`async fn`, `.await` and `block_on`](#async-fn-await-and-block_on) |
| `async`, `await` (JS) | [`async` and `await`](#async-and-await) |
| `AtomicBool`, `Ordering::Relaxed` | [`Arc` and atomics](#arc-and-atomics) |
| Binding catch-all (`r0 =>`) | [Binding catch-alls](#binding-catch-alls) |
| `bool::then` | [Option and Result combinators](#option-and-result-combinators) |
| Borrow ends at last use | [A borrow ends at its last use](#a-borrow-ends-at-its-last-use) |
| `Box<dyn FnOnce>` | [Closures and functions as values](#closures-and-functions-as-values) |
| `Box<T>` | [`Box<T>`, `*` and `&*`](#boxt--and--and-deref-coercion) |
| `build.rs`, `cargo:` lines | [`build.rs` and its `cargo:` lines](#buildrs-and-its-cargo-lines) |
| `CARGO_CFG_TARGET_OS` | [The target in a build script](#the-target-in-a-build-script-cargo_cfg_target_os) |
| `catch_unwind`, `downcast_ref` | [`catch_unwind`](#catch_unwind) |
| `Cell`, `RefCell`, `Rc` | [Changing state through a shared reference](#changing-state-through-a-shared-reference-cell-refcell-rc) |
| `cfg!` | [Macros](#macros) |
| `checked_*`, `saturating_*`, `wrapping_*` | [Overflow](#overflow-saturating_-wrapping_-checked_) |
| Classic scripts (JS) | [Classic scripts and one global per file](#classic-scripts-and-one-global-per-file) |
| `.clone()` | [Moves, `Copy` and `.clone()`](#moves-copy-and-clone) |
| Closure syntax | [Closure syntax and captures](#closure-syntax-and-captures) |
| Closures instead of classes (JS) | [Closures instead of classes](#closures-instead-of-classes) |
| `collect` | [`collect` builds the container](#collect-builds-the-container-the-type-asks-for) |
| `const fn`, `static` | [`static` tables and `const fn`](#static-tables-and-const-fn) |
| `const` in `impl` or function | [`Self`, associated constants](#self-associated-constants-and-local-const) |
| `Copy` | [Moves, `Copy` and `.clone()`](#moves-copy-and-clone) |
| `copy_from_slice` | [Bytes](#bytes-to_le_bytes-from_le_bytes-try_into-copy_from_slice) |
| `CString`, `CStr` | [`CString` and `CStr`](#cstring-and-cstr) |
| `Debug`, `Display` | [`Display` and `Debug`](#display-and-debug) |
| Default parameters (JS) | [Default parameters](#default-parameters) |
| `Default` | [`Default` by hand](#default-by-hand-and-struct-update-syntax-defaultdefault) |
| `DEP_*_LIB_DIR`, `links` | [`links` and `DEP_*` variables](#links-and-dep_-variables) |
| Deref coercion | [`Box<T>`, `*` and `&*`](#boxt--and--and-deref-coercion) |
| Destructuring (JS) | [Destructuring](#destructuring) |
| Disjoint field borrows | [Borrowing two fields at once](#borrowing-two-fields-at-once) |
| Doc comments `///`, `//!` | [Attributes](#attributes) |
| `drop`, `mem::forget` | [`drop(x)` and `std::mem::forget`](#dropx-and-stdmemforget) |
| `Drop` | [`Drop`](#drop) |
| `dyn Trait` | [Trait objects](#trait-objects-dyn-trait-versus-generics) |
| Entry API (`entry`, `or_insert`, `or_default`) | [`Vec`, `vec![]`, maps](#vec-vec-maps-and-the-entry-api) |
| Enums with data | [Enums that carry data](#enums-that-carry-data) |
| `env!`, `option_env!`, `include!`, `include_bytes!` | [Macros](#macros) |
| Error types | [Error types written by hand](#error-types-written-by-hand) |
| `expect`, `unwrap`, `panic!` | [`unwrap`, `expect` and `panic!`](#unwrap-expect-and-panic) |
| Features, optional dependencies | [Features](#features-optional-and-target-specific-dependencies) |
| Field-init shorthand | [Field-init shorthand](#field-init-shorthand) |
| `Fn`, `FnMut`, `FnOnce` | [`Fn`, `FnMut`, `FnOnce`](#fn-fnmut-fnonce) |
| `for` over tuples | [`for` over an array of tuples](#for-over-an-array-of-tuples) |
| `for<'py>` | [Higher-ranked bounds](#higher-ranked-bounds-forpy) |
| `format!`, `format_args!`, format strings | [Format strings](#format-strings), [Macros](#macros) |
| `From`, `try_from`, `into` | [No implicit integer conversions](#no-implicit-integer-conversions-from-try_from-as) |
| `from_le_bytes`, `to_le_bytes` | [Bytes](#bytes-to_le_bytes-from_le_bytes-try_into-copy_from_slice) |
| Generics, trait bounds, `where` | [Generics and trait bounds](#generics-and-trait-bounds-m-reflectmessage-where) |
| Hooks (Preact) | [Preact hooks](#preact-hooks-usestate-useref-useeffect) |
| `if let`, `while let` | [`if let` and `while let`](#if-let-and-while-let) |
| `if` as an expression | [`if` and blocks are expressions](#if-and-blocks-are-expressions) |
| IIFE, `"use strict"` (JS) | [IIFE and `"use strict"`](#iife-and-use-strict) |
| `impl Trait` | [`impl Trait` in argument and return position](#impl-trait-in-argument-and-return-position) |
| `impl Trait for Type` | [Implementing a library's trait](#implementing-a-librarys-trait) |
| `impl` blocks in several files | [One type, many `impl` blocks](#one-type-many-impl-blocks) |
| `impl` on a generated type | [Adding methods to a generated type](#adding-methods-to-a-generated-type) |
| Iterator adapters | [Iterator chains](#iterator-chains) |
| `json!` | [Macros](#macros) |
| `LazyLock`, `OnceLock`, `Once` | [Lazily initialised globals](#lazily-initialised-globals-lazylock-oncelock-once) |
| `let _ =` | [`let _ =`](#let-_--discarding-a-result-on-purpose) |
| Let chains | [Let chains](#let-chains) |
| let-else | [let-else](#let-else) |
| `log!`, `error!`, `debug!` | [Macros](#macros) |
| `Map`, `Set` (JS) | [`Map` and `Set`](#map-and-set) |
| `match` | [`match` is an expression](#match-is-an-expression-and-must-be-exhaustive) |
| Match guards | [Match guards](#match-guards) |
| `matches!` | [`matches!`](#matches) |
| `mem::take`, `mem::replace` | [`std::mem::take` and `std::mem::replace`](#stdmemtake-and-stdmemreplace) |
| `mod`, `mod.rs` | [Modules](#modules-mod-modrs-inline-mod) |
| `move` | [`move` closures](#move-closures-and-cloning-before-the-move) |
| `mpsc`, `recv_timeout` | [Channels](#channels-mpsc-send-recv_timeout) |
| `Mutex`, `MutexGuard`, poisoning | [`Mutex`, guards and poisoning](#mutex-guards-and-poisoning) |
| Newtypes, tuple structs, unit structs | [Tuple structs, newtypes and unit structs](#tuple-structs-newtypes-and-unit-structs) |
| `NonNull` | [`NonNull<T>`](#nonnullt) |
| `null`, `undefined`, `NaN` (JS) | [`null`, `undefined` and `NaN`](#null-undefined-and-nan) |
| Object literals, shorthand (JS) | [Object literals and shorthand properties](#object-literals-and-shorthand-properties) |
| `ok`, `ok_or`, `map_err`, `unwrap_or` | [Option and Result combinators](#option-and-result-combinators) |
| `oneof` | [A protobuf `oneof` is an `Option` of an enum](#a-protobuf-oneof-is-an-option-of-an-enum) |
| `Option::take`, `take_if` | [`Option::take` and `Option::take_if`](#optiontake-and-optiontake_if) |
| Or-patterns, constant patterns | [Or-patterns and constant patterns](#or-patterns-and-constant-patterns) |
| `OUT_DIR` | [Generated code](#generated-code-out_dir-and-include) |
| Patterns in closure parameters | [Patterns in closure parameters](#patterns-in-closure-parameters-b-f-_-c) |
| Promises (JS) | [Promises](#promises) |
| `pub`, `pub(crate)`, `pub(super)` | [Visibility](#visibility-pub-pubcrate-pubsuper) |
| `pub use` | [`use` paths, aliases and re-exports](#use-paths-aliases-and-re-exports) |
| `py.detach` | [`py.detach`: releasing the GIL](#pydetach-releasing-the-gil) |
| `#[pyclass]`, `#[pyfunction]` | [Attributes](#attributes) |
| `Python<'py>`, `Bound`, `Py` | [`Python<'py>`, `Bound<'py, T>`, `Py<T>`](#pythonpy-boundpy-t-pyt) |
| Raw strings, byte strings | [Literals](#literals-raw-strings-and-byte-strings) |
| Raw pointers from references | [References become raw pointers](#references-become-raw-pointers) |
| `Self`, associated constants | [`Self`, associated constants](#self-associated-constants-and-local-const) |
| `self` by value, `self: Box<Self>` | [`self` by value](#self-by-value-and-self-boxself) |
| `Send`, `Sync` | [`Send`, `Sync` and `'static` bounds](#send-sync-and-static-bounds) |
| Shadowing | [Shadowing](#shadowing) |
| Slices and arrays | [Slices and arrays](#slices-and-arrays-u8-16-u8-mut-u8) |
| `spawn_blocking` | [`spawn_blocking` in a Tauri command](#spawn_blocking-in-a-tauri-command) |
| `String`, `&str`, `from_utf8_lossy` | [Strings](#strings-string-str-into-lossy-conversions) |
| Struct update syntax | [`Default` by hand](#default-by-hand-and-struct-update-syntax-defaultdefault) |
| Tagged templates, `html` (JS) | [Template literals and tagged templates](#template-literals-and-tagged-templates-htm) |
| `thread::spawn`, `thread::Builder` | [`std::thread::spawn`](#stdthreadspawn-and-threadbuilder) |
| `thread_local!` | [`thread_local!`](#thread_local) |
| Trait in scope | [A trait's methods need the trait in scope](#a-traits-methods-need-the-trait-in-scope) |
| `transpose` | [Option and Result combinators](#option-and-result-combinators) |
| `try` / `finally` (JS) | [`try` / `finally`](#try--finally) |
| `try_into` (slice to array) | [Bytes](#bytes-to_le_bytes-from_le_bytes-try_into-copy_from_slice) |
| Tuple patterns | [Matching tuples](#matching-tuples) |
| `type` alias | [Type aliases](#type-aliases-type) |
| `unsafe extern "C"` | [`unsafe extern "C"` blocks](#unsafe-extern-c-blocks) |
| `unsafe impl Send` | [`unsafe impl Send`](#unsafe-impl-send) |
| `unsafe { }`, `// SAFETY:` | [`unsafe { }` and `// SAFETY:`](#unsafe---and--safety) |
| `use`, `use ... as _` | [`use` paths](#use-paths-aliases-and-re-exports), [trait in scope](#a-traits-methods-need-the-trait-in-scope) |
| Variant constructor as a function | [A variant constructor used as a function](#a-variant-constructor-used-as-a-function) |
| `Vec`, `vec![]`, `HashMap` | [`Vec`, `vec![]`, maps](#vec-vec-maps-and-the-entry-api) |
| Wire values, `as i32`, `TryFrom<i32>` | [Wire values](#wire-values-integer-constants-as-i32-and-try_from) |
