---
type: readme
status: current
summary: The Host Software Guide - a book-length orientation to the host side of the tag system for a software engineer new to the project, from the system and its communication and storage models down to the architecture and code structure of each component, with the reading order.
---

# Host Software Guide

This guide explains the host software of the Ultralight Tags project to a
software engineer who is new to it. It assumes you can read C++ and have met
an embedded system before, and it assumes nothing about this project, about
Rust, or about the tags. It goes from the top down: what the system is and
what the host does in it, how a host talks to a tag and what the two agree
on, how data is stored on the tag and on the host, and then each component
in turn, its architecture and the layout of its code.

The guide is written for reading in order the first time, and for dipping
into afterwards. Each chapter stands on its own, says what it assumes you
already know, and ends by pointing at the documents and code it summarises.
It does not repeat what those documents say in full: the per-crate READMEs,
design documents, the generated API references and the decision records stay
the detailed sources, and this guide is the thread that connects them.

## Chapters

| | Chapter | Read it to learn |
| --- | --- | --- |
| 1 | [The system](01-system-overview.md) | What a tag, a base and the host are, a tag's life cycle, the host's jobs, the two implementations of the host code, and the vocabulary used everywhere else |
| 2 | [The source tree](02-source-tree.md) | Where everything is, what each directory, crate and program is called, how CMake and cargo divide the build, and where to look things up |
| 3 | [The communication model](03-communication-model.md) | How a host reaches a tag: USB, the ST-LINK protocol, SWD, the monitor, the two transports, the RPC, the timeouts, and why attaching resets the tag |
| 4 | [What host and firmware share](04-shared-contract.md) | Every definition both sides compile: the protobuf messages, the state machine, the configuration model, the monitor ABI, the packed log formats, the identity record and the loader block, and how each is changed |
| 5 | [Data and storage](05-data-and-storage.md) | How a tag stores its log, how a download works, the SQLite file and who writes it, and every file the host keeps |
| 6 | [tagcore: the tag interface](06-tagcore.md) | The Rust crate every tool and application talks to a tag through: its layers, concurrency, errors, generated code and tests |
| 7 | [Programming tags and bases](07-programming-and-flash-tools.md) | tagprobe, probe-rs, the field-safe driver, DFU, and the tag-flash and base-flash tools |
| 8 | [The bridge to the C++ libraries](08-cpp-bridge.md) | tagcore_c and the three Rust wrappers over it: why Rust calls C++, the C contract, the linking per platform, the safety rules |
| 9 | [The command-line tools](09-command-line-tools.md) | The shape every tool follows, what each does, and the download tool in detail |
| 10 | [The application pattern](10-application-pattern.md) | Engine, model and page; threads; the view state and prompts; the Tauri shell; the UI kit; mocks and oracles |
| 11 | [The applications](11-applications.md) | Tag Programmer, Tag Monitor, Tag Calibrator and Tag View: what each does, how it is built from the pattern, and its code map |
| 12 | [External dependencies](12-external-dependencies.md) | Every third-party module the host uses, what it is for, why it was chosen and where it is pinned |
| 13 | [Building and testing](13-building-and-testing.md) | Toolchains, commands, features, environment variables, the test strategy and the documentation checks |
| 14 | [Rust in this code base](14-rust-for-this-codebase.md) | The Rust idioms the code relies on, explained for a C++ engineer, with the places they appear |
| 15 | [Assumptions and invariants](15-assumptions-and-invariants.md) | The rules the code depends on, collected in one place with pointers to where each is enforced |

## How this guide relates to the other documents

| Kind | Where | Answers |
| --- | --- | --- |
| This guide | `host/design/guide/` | How the host software fits together and why, for a newcomer |
| READMEs | beside every directory | What is here, how to build and use it |
| Design documents | `design/` beside the code | How one component works now |
| Decision records | [`docs/decisions/`](../../../docs/decisions/README.md) | What was chosen, what was rejected, the evidence |
| Shared contracts | [`docs/shared/`](../../../docs/shared/README.md) | The definitions host and firmware both compile |
| API references | the developer portal's Doxygen and rustdoc | Each function's contract |
| User manual | `host/docs/` | How to use the programs |
| Bench procedures | [`docs/bench/`](../../../docs/bench/README.md) | How to work with real tags without destroying evidence |

When the code changes in a way that makes a chapter wrong, change the
chapter: it is a design document and follows the
[documentation guide](../../../docs/documentation-guide.md).

## Conventions

- **Names.** A program is called by its executable's name in code font
  (`tagmonitor`) and by its product name in prose (Tag Monitor). The
  [source tree chapter](02-source-tree.md#names) has the full table of
  directories, crates, executables and product names.
- **Paths** are relative to the repository root unless they start with `./`.
- **C++ and Rust.** Where both implementations exist, the guide describes
  the Rust one and says how the C++ one differs. The C++ is older and is
  the reference for wire behaviour.
