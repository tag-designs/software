---
type: readme
status: current
summary: Tag Programmer in Rust - qtprogram's station and programming session ported to a toolkit-free engine, programming through probe-rs instead of STM32CubeProgrammer, with a Tauri shell over a web front end.
---

# Tag Programmer (Rust)

`tagprogram-rs` is the Rust port of [qtprogram](../qtprogram/README.md). It
does the same job: it programs and tests a batch of tags through a USB base
and records each one. It has the same window, checks, texts and record file.
The difference is underneath. It programs through
[probe-rs](../../libraries/tagprobe-rs/README.md) and needs no
STM32CubeProgrammer, and its UI is a web page drawn by Tauri.

## Layout

| Directory | What it is |
| --- | --- |
| `core/` | `tagprogram-core`: the model (`model/`: ELF, flash plan, identity record, image, release catalog, record CSV, settings) and the engine (`engine/`: station, session, view state). No UI toolkit; unit tests run without hardware. |
| `ui/` | The page: `index.html`, `app.css`, `app.js` draw the view state; `mock-engine.js` stands in for the engine in a plain browser. |
| `app/` | The Tauri shell: starts the engine, forwards commands and queries, emits its events. `build.rs` assembles `app/dist/` from `ui/` and [`host/ui-kit`](../../ui-kit/README.md). |
| `design/` | [The UI/engine contract](design/ui-engine-contract.md) and [qtprogram's specification](design/qtprogram-spec.md), the behaviour this port reproduces. |

The engine owns every rule and all state; the page holds none. That split is
what lets the same kit and pattern serve qtmonitor and qtcalibrate later.

## Building and running

```sh
cd host/applications/tagprogram-rs
cargo build -p tagprogram          # the app
./target/debug/tagprogram
cargo test -p tagprogram-core      # model and view tests, no hardware
```

To work on the page without the app or hardware, open `ui/index.html` in a
browser. The mock engine supplies canned states, and a switcher in the
corner changes between them.

`cargo run -p tagprogram-core --example smoke -- <scratch-dir>` runs the
engine headless with scratch settings. It answers the first-run question with
automatic attach off, so it lists and identifies the base but never attaches
to the tag.

`cargo run -p tagprogram-core --example bench -- <scratch-dir> <release> <label> [--retest]`
is qtprogram's `--bench-program`. It attaches (resetting the tag), programs or
retests one tag, prints each step and the record row, and exits 0 for a
written OK row.

Settings live in the platform config directory (`settings.json`, qtprogram's
keys). Downloaded releases go in the platform data directory under
`firmware/`. The record defaults to `Documents/Tag Programmer/programmed-tags.csv`,
as in qtprogram.

## Programming path

- The base is identified with the `F6 99` command
  ([Identifying a Base](../../../embedded/bases/README.md)).
- A base that answers is driven by probe-rs's own ST-LINK driver. One that
  does not (older field firmware) is driven by tagprobe's field-safe driver.
- `tagprobe::program` erases as the image's flash plan says, then programs
  and verifies in one session. It clears `FLASH_SR.PEMPTY` on an STM32L432,
  then starts the image as `-g 0x08000000` did.
- The read-back for Retest uses `tagprobe::read_memory`.

The probe driver and part are reported on the Programmer and SWD rows, and
probe-rs's progress goes to the log pane.

## Status

- Program and Retest have passed on a BitTag (bittag-base-v7, stock
  driver) with fw-v0.6, through the bench runner: mass erase, program,
  verify, ALL_PASSED, erased and IDLE in 13 s, and the flash read back
  matching the image.
- Retest has passed on an IMUTagNandBmp581 (U375, tag-breakout-base-l432-u375-1v8,
  stock driver): the flash read back matching fw-v0.6, and ALL_PASSED.
  Programming a U375 through the engine has not been run yet.
- Not yet run through a field base (field-safe driver). Not packaged.
