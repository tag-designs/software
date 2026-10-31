---
type: design
status: current
summary: Chapter 15 of the Host Software Guide - the assumptions and invariants the host code depends on, collected in one place and grouped by subject (the tag and its firmware, the link, the shared contract, data and files, the C++ bridge, the applications, the build), each with where it is enforced or documented, so a change can be checked against them.
---

# 15. Assumptions and invariants

Everything here is stated somewhere in the chapters above or in the
documents they point to; this chapter collects it so a change can be read
against the whole list. Each item names where it is enforced, documented or
was learned the hard way. **Bench rules** are the subset the root `AGENTS.md`
states for working with real tags, and are not repeated in full.

## The tag and its firmware

| Assumption | Where |
| --- | --- |
| **Attaching resets the tag** and ends whatever it was doing; there is no way to observe a running tag except a current trace. Never attach to a running tag to see what it does | chapter 3; `docs/bench/verifying-firmware.md`; every attach method's doc comment |
| **An acknowledgement is not a completion.** Stop, Start, Erase, self test and SetRtc are accepted, then acted on; poll `Status` for the state you asked for, with a timeout | chapter 3; every tool's settle and poll loops; Tag Monitor's monitor model judges a start by the state reached |
| **A host must keep talking.** An L432 session ends after 60 s without a request (5 s on UIUCTag fw-v0.6 and earlier), a U375 one after 5 to 10 s without activity; an IDLE tag then sleeps and answers no SWD until reset | `docs/shared/monitor-interface.md`; the 400 ms status polls; `Tag::keep_alive`; the App Nap opt-out |
| **IDLE means an empty log.** A tag holding data is FINISHED, ABORTED or EXCEPTION; starting an IDLE tag needs no erase | `restart-recovery.md`; `tag-start` refuses other states |
| **Only the firmware that wrote a log erases it.** Hosts send Erase; they never flash over a tag holding data | the flashing protocol in `AGENTS.md`; `tag-flash` touches no external flash |
| **The firmware gates requests by state** and answers `PERM` otherwise; the host does not pre-check beyond what its UI needs to enable buttons | chapter 4's table; `monitor_request_allowed()` |
| **Never start a tag after programming**, and never send SetRtc to a started tag: a PresTag start leaves its RTC write-protected and the SetRtc hangs the firmware | decision 0035; `embedded/tags/TODO.md` |
| **The firmware is the authority on what it does with a field.** A default configuration can omit a field the firmware reads; the host then passes it through | `configuration-model.md` |
| **Supply voltage per target**: 2.5 V for the L432 tags, 3.7 V for IMUTag; the L432's absolute maximum is 3.6 V, and Tag Programmer's Supply check fails above it | `AGENTS.md` step 0; `station.rs` |

## The link

| Assumption | Where |
| --- | --- |
| **One base, one owner.** A base is one claimable USB interface; the monitor and the programmer never hold it at once, and two processes cannot share it | chapter 3; Tag Programmer's `worker_busy`; the C++ recovery library's session model |
| **The link is reliable.** The RPC has no checksum or sequence number; lengths travel in the command operand and result word | `rpc.rs` |
| **Timeouts are polls, not seconds**: an unanswered L432 protobuf call takes about 120 s, during which the `Tag`'s lock is held | `monitor/mod.rs`; Tag Programmer's engine design |
| **Memory access is 32-bit** and word-aligned on the memory path; the RPC buffer is transferred in whole words | `rpc.rs`, `target.rs` |
| **After probe-rs releases a field base, the monitor's first reads may fail** (`0x81`) while the tag boots; retry, and rate-limit the log | `target.rs`; the attach retry in Tag Programmer |
| **An L432 call buffer may hold a request a failed call left pending**; wait for `MON_REQ` and `MON_PEND` to clear before writing it | `l4.rs` `wait_l4_idle`; the known firmware defect in `monitor-interface.md` |
| **Debug access in Sleep must stay on** (`DBGMCU_CR.DBG_SLEEP`) during an L4 session, or polls fail in bursts | the 0x81 investigation; `l4.rs` |
| **probe-rs probes are opened through `StLinkFactory`, never `Lister`** (hidapi is single-threaded on macOS) | `tagprobe`'s `open_probe` |
| **The field-safe driver is not a general debugger**: byte writes patch whole words, which is unsafe for peripheral registers with read side effects | `fieldsafe.rs` |

## The shared contract

| Assumption | Where |
| --- | --- |
| **Field numbers never change or get reused**; removed ones are `reserved`; unknown fields are skipped | `proto/README.md` |
| **proto3 cannot say whether a zero was set**; the default configurations say which fields a tag has | chapter 4; `ConfigShape` |
| **`monitor.h` and `abi.rs` match**; a change to one is a change to the other in the same commit; the U375 checks the ABI version at attach, the L432 cannot | `abi.rs` and its layout test |
| **The packed log structs are little-endian, packed, size-asserted, and decoded only by the C++ writer**; no Rust code interprets a record | `binary-datalogs.md`; chapter 5 |
| **The identity record is at a fixed address per MCU** (`0x1A0` and `0x240` after the flash base) and unknown entries are skipped | decision 0021; `identity.rs` |
| **`intflashsz` is in kilobytes**; `extflashsz` in bytes | `tagdata.proto`; `chip_for` |
| **A base that answers `F6 99` has the ST-LINK fixes**; one that does not is driven field-safe | `tagprobe`; `embedded/bases/README.md` |
| **The nanopb bounds equal the C layouts** they carry; the firmware asserts it | `binary-datalogs.md` |

## Data and files

| Assumption | Where |
| --- | --- |
| **The writer decides how far a page advances the download and where it ends**; the next request waits for the writer's answer | chapter 5; every download loop |
| **One SQLite writer, one schema.** Rust writes through it, never a copy | decision 0037; chapter 8 |
| **A missing measurement is a missing row**, never a placeholder | `sqlite-logs.md` |
| **Schema metadata describes data; display policy lives in viewer preferences** | `AGENTS.md`; the `streams` table |
| **Downloading changes nothing on the tag**, so a download can be repeated and compared | chapter 5 |
| **Configuration files carry proto field names and every field**, and are merged by the tag type's shape on Open and `--merge` | `config-files.md`; `configuration-model.md` |
| **The record CSV is append-only**; a row for every attempt; the file must be appendable before a tag is touched | Tag Programmer's engine design |
| **A capture is taken before anything runs the firmware**, because a boot rewrites the evidence | `swd-recovery.md`; `capturing-a-tag.md` |
| **Preference files are per tag type**; a saved file is written as the Qt application writes it, so both front ends read it | `sensorviz-session` tests |
| **The calibration capture's schema is qtcalibrate's**, so captures replay in both programs | `capture.rs`; decision 0039 |

## The C++ bridge

| Assumption | Where |
| --- | --- |
| **No exception crosses the boundary**; every entry point is guarded | `taglogwriter_c.cc` and its siblings |
| **The ABI version is checked before any other call**; a mismatch is refused with a message | each wrapper's constructor |
| **Protobuf messages cross as wire bytes**; numbers as C types; descriptions as JSON the C side writes | chapter 8 |
| **A handle is used by one thread at a time** (`Send + !Sync`, `&mut self`) | each wrapper's safety model |
| **The library's strings live until the next call on the handle** | the wrappers copy them at once |
| **Two SQLite copies must never open one file in one process** | `tagcore-c` design |
| **The link follows the platform's packaging**: static on macOS, shared on Linux and Windows | decision in the `tagcore-c` design, 2026-10-27 |
| **The calibration arithmetic is bit-identical to Qt's** and stays so until a measured change; the solver's refit cadence is a function-level static, one session per process | decision 0039 and its consequences |

## The applications

| Assumption | Where |
| --- | --- |
| **The engine owns every rule and all state; the page holds none** | chapter 10; each contract's "deliberate differences" are the only exceptions |
| **The whole view state is published, only when it changed**; the page renders all of it | the engine loops |
| **One thread touches the tag**; the engine never waits on it (Tag Programmer runs quick monitor calls on its engine thread and slow work on workers) | each architecture document |
| **Every dialog is a prompt answered by id**; an answer to another id is ignored | each contract |
| **Nothing happens until the page sends `ready`**; the first `ready` attaches unless told not to | Tag Monitor and Tag Calibrator contracts |
| **The Qt application is the reference**; its test files are the oracles and change with its behaviour | chapter 13 |
| **Do not open user files inline during start-up** on macOS: a privacy prompt blocks the engine thread and blanks the window | Tag Programmer's engine design |
| **The page opens from `file://` against its mock**; no build step, no ES modules, vendored libraries | `ui-kit/README.md` |
| **The mock's samples are hand-written** and must be kept in step with the contract | each `mock-engine.js` header |
| **Quit goes through the engine**, which asks about unsaved work and detaches before the window closes | each shell's close hook |

## The build

| Assumption | Where |
| --- | --- |
| **One workspace, one lock, `--locked` in every CMake and CI build**; a dependency change updates `host/Cargo.lock` and the four notices files | decision 0041 |
| **A CMake build writes nothing into the source tree**; cargo's output goes to `<build>/host/cargo`, the page is staged under `OUT_DIR` | `HostRust.cmake`; the `app` build scripts |
| **Crates that link `libtagcore_c` need `TAGCORE_C_LIB_DIR`**; `cargo check` and `cargo doc` need only an empty directory and `TAGCORE_C_LINK=dylib` | chapter 13 |
| **libusb and protoc are vendored**; Linux needs `libudev` and WebKitGTK 4.1 | chapter 12 |
| **Every item is documented**, private ones included; rustdoc builds with warnings denied | the lints in every `Cargo.toml` |
| **The C++ is the wire reference** for `tagcore`; the two are kept in step by hand, file for file | chapter 6 |
| **A release is qualified on hardware** before it is flashed onto a field tag, and the macOS package is signed and notarised locally from the CI build | `docs/release/` |
