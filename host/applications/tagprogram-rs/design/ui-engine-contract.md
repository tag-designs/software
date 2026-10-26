---
type: design
status: current
summary: The contract between Tag Programmer's engine and its web UI - the view state the engine publishes, the commands and queries the UI sends, the prompts - and the model API the engine is built on.
---

# Tag Programmer: UI/engine contract

Tag Programmer is the Rust port of qtprogram. It is split the way the
[config-form mockup](../../qtmon/design/proposals/config-form-mockup/README.md)
and the [toolkit-neutral core proposal](../../sensorviz/design/proposals/toolkit-neutral-core.md)
describe:

- **`core/`** (`tagprogram-core`): the model and the engine. No UI toolkit.
  The engine owns every rule and all state.
- **`ui/`**: a generic renderer. It draws the view state it is sent and turns
  user actions into commands. It holds no rules.
- **`app/`**: the Tauri shell. It runs the engine, forwards commands and
  queries, and emits the engine's events to the page.

The behaviour to reproduce is qtprogram's, as specified in
`qtprogram-spec.md` beside this file. Where this port deliberately differs,
it says so below.

## Deliberate differences from qtprogram

- **No STM32CubeProgrammer.** Programming, the connect check and the flash
  read-back use probe-rs through `tagprobe-rs`: probe-rs's own ST-LINK driver
  for a base that answers the identify command (`F6 99`), and the field-safe
  driver for one that does not. The **Programmer** check reports the probe
  driver; there is no "Choose Programmer..." and no programmer path setting.
- The **Base** check shows the base's identity when it has one.
- The programmer's own output (probe-rs progress) does reach the log pane.
- **No final start, stop and erase.** As qtprogram since a05900f6, the
  session ends after the self-tests with the tag IDLE: a start belongs to a
  deployment, and on a PresTag it left the RTC locked so the clock setting
  after the final erase hung the firmware (`embedded/tags/TODO.md`).
  `qtprogram-spec.md` describes the earlier sequence.
- A program failure reads "Programming failed (<error>). The probe's
  messages are in the log." and the Retest read-back failure reads "probe-rs
  could not read it (<error>)", in place of the STM32_Programmer_CLI texts.
- The record's `host_version` is `tagprogram-rs <version>`, so its rows can be
  told from qtprogram's.
- Once the session ends, its message stays on the step line until Next Tag.

## Transport

In the app, `window.__TAURI__` is present (`withGlobalTauri`):

- `invoke("command", { cmd, args })` -- fire and forget; returns `null` or an
  error string.
- `invoke("query", { name, args })` -- returns the answer as JSON.
- Events from the engine: `listen("state", e => e.payload)`,
  `listen("log", ...)`.

In a plain browser (`ui/index.html` opened as a file), `ui/mock-engine.js`
implements the same three calls with canned data so the page can be designed
without the app or hardware. `host/ui-kit/kit.js` hides the difference behind
one `engine` object (see the kit's README).

## View state (event `state`)

The engine sends the whole state after every change. All text is final, ready
to show; the UI never composes messages.

```jsonc
{
  "title": "Tag Programmer v2.0",
  "firmware": {
    "items": [                       // in order, as the combo shows them
      { "id": "release:fw-v0.7", "text": "fw-v0.7  (2026-10-15)" },
      { "separator": true },
      { "id": "file:/abs/path/BitTag.elf", "text": "BitTag.elf  (/abs/path)" },
      { "separator": true },
      { "id": "other", "text": "Other ELF file..." }
    ],
    "selected": "release:fw-v0.7",   // "" selects the "Choose firmware" item
    "enabled": true,
    "releaseNotesUrl": "https://github.com/..."   // null disables the button
  },
  "tagType": {
    "items": ["BitTag", "PresTag"],
    "selected": "BitTag",            // null: no selection
    "enabled": true,
    "note": "detected from the tag"  // or "from the image", or ""
  },
  "checks": [                        // always these 8, in this order
    { "name": "Programmer", "level": "ok",   "text": "probe-rs 0.32.0" },
    { "name": "Base",       "level": "ok",   "text": "ST-LINK 0037..." },
    { "name": "SWD",        "level": "ok",   "text": "0x435 STM32L43xxx/STM32L44xxx" },
    { "name": "Supply",     "level": "ok",   "text": "3.23 V" },
    { "name": "Tag",        "level": "ok",   "text": "BitTag V6, 56e5e6a0" },
    { "name": "Tag state",  "level": "ok",   "text": "IDLE" },
    { "name": "Firmware",   "level": "ok",   "text": "BitTag (fw-v0.6), verified against its manifest" },
    { "name": "Record",     "level": "ok",   "text": "programmed-tags.csv (1 row)" }
  ],                                 // level: "ok" | "warn" | "fail" | "pending"
  "rowButtons": {
    "recordChoose": true,            // "Choose..." on the Record row
    "stop":  { "visible": false, "enabled": true },   // on the Tag state row
    "erase": { "visible": false, "enabled": true }
  },
  "tag": {
    "label": "BitTag-017 (recorded)",
    "uuid": "2035374D303150190059002F",
    "state": "IDLE",
    "firmware": "BitTag  56e5e6a0  (BitTag V6)",
    "tests": "ALL_PASSED",
    "clock": { "text": "+0.02 s", "warn": false },   // warn: amber text
    "syncEnabled": true,
    "result": { "text": "", "ok": null }             // ok: true green, false red
  },
  "actions": {                       // tooltip shown when disabled
    "program": { "enabled": true,  "tooltip": "" },
    "retest":  { "enabled": true,  "tooltip": "" },
    "nextTag": { "enabled": true,  "tooltip": "" },
    "attach":  { "enabled": true,  "tooltip": "" }
  },
  "session": {
    "stepText": "Programming BitTag (fw-v0.6): ...",  // "" when idle
    "progress": null,                // null hidden; "busy"; or {"done":3,"total":40}
    "retryVisible": false,
    "cancelVisible": false,
    "cancelText": "Cancel"           // or "Give Up"
  },
  "batch": "Batch: 0 programmed this session, 0 not OK",
  "autoAttach": true,                // the Station menu's check mark
  "recordPath": "/Users/.../programmed-tags.csv",     // for Show CSV
  "prompt": null                     // see Prompts
}
```

## Log (event `log`)

`{ "level": "INFO", "text": "..." }`, one per line, already filtered to the
log level the UI last set (`set_log_level`). Levels: TRACE, DEBUG, INFO, WARN,
ERROR. The UI appends `"<LEVEL> <text>"` lines to the log pane.

## Prompts

When the engine needs an answer it sets `state.prompt` to one of these, with a
fresh `id`. The UI shows the dialog once per `id` and answers with the named
command. Until answered the prompt stays in the state.

| `kind` | Fields | Dialog (qtprogram's) | Answer |
| --- | --- | --- | --- |
| `autoAttach` | -- | "Attach Automatically?" question | `answer_auto_attach {on}` |
| `recordFile` | `defaultPath`, `exists` | "Record File" box: Use It/Create, Open Existing..., Create Elsewhere..., Cancel | `record_use_default`, `record_open {path}`, `record_create {path}`, `prompt_cancel` |
| `chooseBase` | `label`, `items[]` (text), `current` (index) | "Choose a Base" list | `choose_base {index}`, `prompt_cancel` |
| `alert` | `title`, `text`, `style` (`"warning"`/`"info"`) | message box | `prompt_cancel` |

## Commands (`invoke("command", {cmd, args})`)

| `cmd` | `args` | Does |
| --- | --- | --- |
| `start` | -- | begin(): first-run prompt, checks, catalog refresh |
| `choose_firmware` | `{id}` | an item from `firmware.items` (`release:`/`file:` ids); `other` is handled by the UI's file dialog, then `choose_file` |
| `choose_file` | `{path}` | program a local ELF |
| `choose_target` | `{name}` | tag type |
| `refresh_releases` | -- | Station > Refresh Firmware Releases |
| `attach` / `next_tag` | -- | buttons |
| `stop_tag` / `erase_tag` | -- | row buttons (the UI confirms Erase first, with qtprogram's text) |
| `sync_clock` | -- | Sync |
| `program` | `{label}` | after the Program dialog's "Erase and Program" |
| `retest` | `{label}` | after the Retest dialog |
| `retry_connect` / `cancel_session` | -- | session buttons |
| `set_auto_attach` | `{on}` | Station > Attach Automatically |
| `list_bases` | -- | Station > Choose Base... (raises `chooseBase`, or an `alert`) |
| `choose_record` | -- | Station > Choose Record File... (raises `recordFile`) |
| `answer_auto_attach`, `record_use_default`, `record_open`, `record_create`, `choose_base`, `prompt_cancel` | see Prompts | |
| `set_log_level` | `{level}` | log pane combo |
| `save_text` | `{path, text}` | Save Error Log (truncates; qtprogram's did not) |

## Queries (`invoke("query", {name, args})`)

| `name` | `args` | Returns |
| --- | --- | --- |
| `program_preview` | -- | `{ ok, reason, tag, image, erase, lost[], kept[], notes[] }` for the Program dialog; `ok:false` with `reason` instead of opening it |
| `label_notes` | `{label}` | `notes[]` for the label as typed (recorded-label warnings), shown with `program_preview.notes` |
| `retest_default_label` | -- | the last label recorded for this tag's UUID, or `""` |
| `about` | -- | `{ title, text }` |
| `user_guide_url` | -- | URL or `null` |

## Model API (`core/src/model`)

The engine is built on these. Each is pure logic over files, with unit tests;
none touches USB.

- `elf.rs` -- minimal ELF32 little-endian reader: symbol lookup by name, and
  loadable segments (`PT_LOAD` with file data, at their *physical* address).
- `flashplan.rs` -- `FlashPlan { erase: Erase, error, no_layout, page_size,
  last_erased_page }`, `Erase::{Invalid, None, BelowCalibration, All}`,
  `plan_flash(path)`, `summary()`, `keeps_calibration()`,
  `keeps_configuration()`, `record_label()` (`"none"`, `"below-calibration"`,
  `"all"`). qtprogram's `flashplan.cpp` exactly.
- `identity.rs` -- `ImageIdentity` and `read_image_identity(path)`: the TGID
  record at `0x08000000 + 0x1A0` or `+ 0x240` (port of
  `recovery/identityrecord.cc`'s parsing, limited to what qtprogram reads).
- `image.rs` -- `FirmwareImage` (fields as qtprogram's), `load_firmware_image
  (path, release: Option<&str>)`, `display_name()`, `version()`, `usable()`,
  `image_flash_range(path) -> Result<(start, len), String>`,
  `compare_image_flash(path, start, &[u8]) -> Option<String>` (None when it
  matches), `sha256_of_file`, `same_commit(a, b)`.
- `catalog.rs` -- `Catalog` over a cache directory: `load()` from
  `releases.json`, `refresh()` (blocking GitHub listing), `fetch(tag)`
  (blocking download, extract with the `tar`/`flate2` crates, verify, install,
  `.verified`), `releases()`, `release(tag)`, `is_cached`, `targets(tag)`,
  `image_path(tag, target)`. qtprogram's `firmwarecatalog.cpp` rules and
  messages.
- `record.rs` -- `RecordLog`: `open`, `create`, `append(row)`, `rows()`,
  `labels_for_uuid`, `uuids_for_label`, `writable()`, RFC 4180 quoting,
  qtprogram's header and messages.
- `settings.rs` -- `Settings`: a JSON file with qtprogram's keys
  (`firmwareRelease`, `firmwareFile`, `recentFiles`, `target`, `recordPath`,
  `baseSerial`, `resetsAcknowledged`, `lastLabel`), absent and present
  distinguished.
