---
type: decision
status: accepted
summary: Tag Programmer was ported to Rust as tagprogram-rs - a toolkit-free engine publishing whole view states to a Tauri web UI built on a shared kit, programming through probe-rs (with a field-safe driver for old bases) instead of STM32CubeProgrammer, shipped inside the existing CMake host packages; neither programmer starts the tag any more.
---

# 0035. Host: Tag Programmer in Rust (tagprogram-rs)

Date: 2026-10-26

## Context

qtprogram ([0034](0034-host-qtprogram-batch-station.md)) programs through
STM32_Programmer_CLI. That ties every station to an installed
STM32CubeProgrammer (about 1.7 GB), runs a process per step, and is slow.
On 2026-10-24 replacing the CLI was judged not worth it. The options then
were ST's API library (a Qt version clash) or libstlink (no STM32U3
support).

Two things changed:

- The [probe-rs evaluation](../investigations/2026-10-probe-rs-evaluation.md)
  showed that probe-rs programs both the STM32L432 and STM32U375 tags,
  through current base firmware with its own driver, and through field base
  firmware with a small driver of ours.
- A Rust tagcore ([tagcore-rs](../../host/libraries/tagcore-rs/README.md))
  existed and matched the C++ on the bench.

The user asked for a Rust Tag Programmer that looks like qtprogram, with a UI
approach general enough for the other host applications.

## Decision

- **Port qtprogram's behaviour, not its code structure.** The station's
  checks, texts, record columns and results are qtprogram's. The
  [UI/engine contract](../../host/applications/tagprogram-rs/design/ui-engine-contract.md)
  lists every deliberate difference.
- **A toolkit-free engine and a dumb UI.** `tagprogram-core` owns all state
  on one thread and publishes the whole view state as JSON after each change.
  The UI sends commands and queries and holds no rules. The same split is
  meant to serve qtmonitor and qtcalibrate later.
  ([Engine design](../../host/applications/tagprogram-rs/design/engine.md).)
- **Tauri with a plain web page, on a shared kit.** `host/ui-kit` gives the
  page a macOS-native look in plain CSS, plus the engine connection, menus,
  dialogs and file pickers. There is no front-end framework or bundler, and
  the page also runs in a browser against a mock engine.
- **Program through probe-rs**, via `tagprobe`. A base that answers the
  `F6 99` identify command gets probe-rs's ST-LINK driver; any other base gets
  the field-safe driver, since field bases cannot be reflashed.
  ([tagprobe design](../../host/libraries/tagprobe-rs/design/architecture.md).)
- **Clear `FLASH_SR.PEMPTY` after programming an STM32L4**, only when it is
  set, as `embedded/tools/flash_release.py` does. 0034 rejected this for
  qtprogram, which writes through the CLI; in tagprobe it is one checked
  register write in the same session as the program. The power-cycle prompt
  remains for a tag that still does not answer.
- **Ship inside the existing CMake/CPack packages** (`BUILD_RUST_APPS`),
  beside the Qt apps, signed by the same code. The CI release workflow builds
  it on all three platforms.
  ([Build and packaging](../../host/applications/tagprogram-rs/design/build-and-packaging.md).)
- **Never start the tag after programming**, in either programmer.
  qtprogram's sequence ended with start, stop, erase and set-clock. On a
  PresTag the start left the RTC write-protected, and the SetRtc that followed
  hung the firmware for the monitor's whole timeout. A start belongs to a
  deployment. The session now ends IDLE after the self-tests. The firmware
  defect, accepting SetRtc after a start, is in `embedded/tags/TODO.md`.
- **Confirm a label already recorded for another tag.** qtprogram only noted
  it in the Program dialog. Tag Programmer asks again before going on, with
  Cancel the default.

## Alternatives considered

- **egui.** Immediate-mode and pure Rust, but its widgets do not look like
  qtprogram's, and a look built in it would not carry over to apps with
  plots and 3-D views. A web page can use existing plotting libraries.
- **Slint.** Considered and declined on 2026-10-24 for the Qt apps as a
  whole. A Slint UI would not serve the other apps' plotting needs any better
  than the web page does.
- **Keep STM32_Programmer_CLI.** It works, but needs CubeProgrammer on every
  station, and it is the slow part of qtprogram.
- **Package with Tauri's bundler as a separate installer.** That would make a
  second signing and notarising path, and a second download. Installing into
  the existing package keeps one of each.
- **Keep the final start in tagprogram-rs only.** That would have kept the
  hang that it caused. The user's direction was that the programmer should
  not call start at all.

## Evidence

- **Bench results:** in the [README's status](../../host/applications/tagprogram-rs/README.md#status).
  - Program and Retest on a BitTag, through a current base (stock driver) and
    through a "BitTag Base V5C" (field-safe driver);
  - Retest on an IMUTag (STM32U375);
  - Program on a UIUCTag (STM32L432KB) through a field base.
- **Batch use:** a batch of PresTags was programmed in one session. The user
  found it "much faster than the version using the stm32 programmer".
- **The PresTag hang:** captured on tag 92 before reset
  (`captures/2026-10-26-prestag92-rtc-lock/`). RTC write protection was on,
  with the RTC's INIT bit clear and LSE running, after the start.
- **Size:** the macOS app is about 21 MB, against qtprogram's 51 MB plus
  STM32CubeProgrammer.
- **CI:** the release workflow built and package-tested all three platforms
  (run 40403579411).
