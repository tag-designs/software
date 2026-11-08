---
type: readme
status: current
summary: "A code-reading tutorial on the Rust host tools for an experienced C/C++ engineer new to Rust -- tagcore from USB to the Tag API, recovery, programming, the C++ and Python bridges, the command-line tools, and the Tauri applications from engine to page, quoting the real code and explaining each library and Rust idiom where it appears."
---

# The Rust host tools: a code-reading tutorial

This tutorial reads the Rust implementation of the host tools with you,
file by file. It is written for an engineer who knows C and C++ well, knows
the tags, the bases, ST-LINK, SWD and the firmware's monitor, and wrote or
used the original C++ tools, but has little Rust and has not met the
libraries this code uses.

It complements the [Host Software Guide](../guide/README.md). The guide is
the map: what the system is, how the parts fit and why. This tutorial is the
walk: it opens the files, quotes the code, follows calls across layers, and
stops wherever a Rust construct or a library would make a C++ reader pause.
Where the guide already explains a design, the tutorial links to it instead
of repeating it.

It is not a Rust course. [The Rust Programming Language](https://doc.rust-lang.org/book/)
is, and chapters 4 (ownership), 6 (enums), 9 (errors), 10 (traits and
generics), 13 (closures and iterators) and 16 (threads) cover most of what
you will meet here. Read them when a construct keeps recurring; the
tutorial explains each one where it first matters.

Sensor View (sensorviz) and its libraries are out of scope.

## How each chapter works

- It opens with what the part does, the files it covers, and what you will
  be able to follow after reading it.
- Code is quoted verbatim from the tree, each excerpt with a link to the
  file and line. `// ...` marks an elision.
- **Library:** paragraphs introduce each third-party crate the first time
  it is used: what it is, and the handful of calls this code makes.
- **Rust:** and **JavaScript:** asides explain a construct where it occurs,
  with the nearest C++ equivalent and why it is used there.
- Each chapter ends with **Try it** (things to read, grep, test or open
  that need no hardware) and **Where next**.

Line numbers were correct when the tutorial was written and drift as the
code changes; the function names in the text are the stable reference.

## Chapters

| | Chapter | You will be able to |
| --- | --- | --- |
| 1 | [The workspace and the build](01-workspace-and-build.md) | Read a `Cargo.toml`, find any crate, build and test one, and see how CMake drives cargo |
| 2 | [A field guide to the Rust and JavaScript in this code](02-rust-and-js-field-guide.md) | Look up an idiom you met in the code and find where else it is used |
| 3 | [The link: USB, the ST-LINK protocol and SWD access](03-usb-and-stlink.md) | Follow a debug-register read from the call to the USB endpoint and back |
| 4 | [The monitor: attach, the two transports and the RPC](04-monitor-and-rpc.md) | Follow an attach and one RPC on the DebugMonitor and mailbox transports |
| 5 | [Protobuf and the Tag API](05-protobuf-and-the-tag-api.md) | See how `.proto` becomes Rust, and follow `Tag::status` down to the monitor |
| 6 | [Recovery: capture, SRAM calls and external flash](06-recovery.md) | Follow a capture and an external-flash read on a halted tag |
| 7 | [Programming tags and bases](07-programming-tags-and-bases.md) | Follow `tag-flash` through probe-rs and `base-flash` through DFU |
| 8 | [The bridges: C++ and Python](08-bridges-to-cpp-and-python.md) | Read the `unsafe` FFI code and the PyO3 module, and say why each call is sound |
| 9 | [The command-line tools](09-command-line-tools.md) | Read any tool, and follow `tag-dwnld` from arguments to SQLite file |
| 10 | [The Tauri shell](10-tauri-shell.md) | Say how a Rust program gets a window through the shell the four applications share (`tagapp-shell`), and list every call between page and Rust |
| 11 | [The engine: threads, the model and the view state](11-engine-and-model.md) | Follow a message through the engine loop and a request through the tag thread |
| 12 | [The page: the UI kit, Preact and htm](12-ui-kit-and-pages.md) | Read a page, change a panel, and preview it against the mock in a browser |
| 13 | [End to end: Tag Monitor's Start](13-tracing-start.md) | Follow one button press from the page to the ST-LINK and back to the redraw |
| 14 | [The three applications compared](14-the-three-applications.md) | Find your way around Tag Programmer and Tag Calibrator by their differences from Tag Monitor |

## Suggested orders

- **Bottom up**, the order of the chapters: 1, then 3 to 9 for the tag
  interface and the tools, then 10 to 14 for the applications. Keep chapter
  2 open beside you.
- **Top down**, starting from something you can click: 1, 13 (the whole
  path at once), then whichever layer chapter 13 sent you to.
- **Just the applications:** 1, 10, 11, 12, 14, and 5 for the `Tag` API they
  call.

## Following along

Everything in **Try it** runs without a tag. Builds go outside the source
tree; on this machine:

```sh
cd host
export CARGO_TARGET_DIR="/Users/geobrown/codedev/Build/tutorial-check"
cargo test -p tagcore --lib            # the tag interface's unit tests
cargo doc -p tagcore --document-private-items --open   # the API reference, private items included
```

and the pages open straight from disk in a browser against their mock
engines, for example `host/applications/tagmonitor-rs/ui/index.html?scenario=idle`
(chapter 12 lists the scenarios).

Anything that attaches to a tag follows the bench rules in
[`docs/bench/`](../../../docs/bench/README.md): attaching resets the tag,
so never attach to a running one to see what it is doing.
