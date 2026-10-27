---
type: design
status: current
summary: The contract between Tag Monitor's Rust engine and its web UI - the threads, the view state the engine publishes, the prompts that replace qtmonitor's dialogs, and the commands and queries the UI sends.
---

# Tag Monitor (Rust): UI/engine contract

Tag Monitor in Rust is split as [Tag Programmer](../../tagprogram-rs/design/ui-engine-contract.md)
is:

- **`core/`** (`qtmon-core`): the model (`src/model`, the port of
  `qtmon/core`) and the engine (`src/engine`). No UI toolkit. Every rule
  and all state are here.
- **`ui/`**: a page on [`host/ui-kit`](../../../ui-kit/README.md) that draws
  the view state and turns clicks into commands. It holds no rules.
- **`app/`**: the Tauri shell, which runs the engine, forwards commands and
  queries, emits the engine's events, and turns a window close into `quit`.

The behaviour is qtmonitor's (`MainWindow`, `TagStatusStrip`, and the
document side of `ConfigTab`). Where this port differs, it says so below.

## Threads

Two threads, as qtmonitor has after its restructure
([architecture](../../qtmon/design/architecture.md)):

| Thread | Owns | Does |
| --- | --- | --- |
| tag (`engine/tagthread.rs`) | the `tagcore::Tag` | polls status every 400 ms while attached; runs commands in order; runs downloads (`engine/download.rs`) |
| engine (`engine/monitor.rs`) | the `MonitorModel`, the `ConfigDocument`, the prompts, the progress displays | applies commands and the tag thread's reports; publishes the view state |

The engine thread never touches the tag and never waits for it, so the view
stays live during a download, and a query is answered after at most the
messages already queued. After Synchronize Clock or Self-Test is accepted the
tag thread pauses its polls for 1.5 s, as qtmonitor does (base 0x81 bursts on
L4 tags; `embedded/bases/TODO.md`).

## Transport

As Tag Programmer's: `invoke("command", {cmd, args})`, `invoke("query",
{name, args})`, and events `state` and `log`. In a plain browser
`ui/mock-engine.js` stands in, with scenarios (`?scenario=`).

Nothing happens until the page sends `ready`. The first `ready` attaches
(unless `QTMON_NO_ATTACH` is set in the environment), as qtmonitor does when
its window opens; every `ready` makes the engine send the whole state again.

## View state (event `state`)

```jsonc
{
  "title": "Tag Monitor v2.1 — COMPASSTAG from tag",
  "modified": false,                  // the document has unsaved changes
  "attaching": false,                 // bases being listed or the tag attached
  "strip": {                          // TagStatusStrip; blank values when detached
    "attached": true,
    "type": "COMPASSTAG", "typeTooltip": "Attached tag type; board UUID 2036...",
    "state":  { "text": "IDLE", "warn": false },      // "IDLE (no answer 4 s)", warn
    "clock":  { "text": "+0.00 s", "warn": false },   // warn beyond 2 s
    "voltage": "2.84 V",
    "test":   { "text": "ALL_PASSED", "warn": false }, // "Running" while one runs
    "approved": false, "approvalEnabled": true
  },
  "tag": {                            // the Tag page
    "enabled": true,                  // false: greyed, still showing the last tag
    "info": { "tagType": "...", "board": "...", "firmware": "...", "flash": "256KB",
              "flashExt": "4MB", "uuid": "...", "gitUrl": "...", "sourcePath": "...",
              "gitHash": "...", "buildDate": "..." },   // null before any attach
    "internalCount": "128",
    "externalCount": "512",           // null hides External Data
    "downloadEnabled": false
  },
  "config": {                         // the Configuration tab
    "document": true, "name": "COMPASSTAG from tag", "tagType": "COMPASSTAG",
    "modified": false,
    "panels": [                       // the shown panels, in page order; see The form
      { "name": "IntervalSchedule", "title": "Logging Schedule", "controls": [ ... ] }
    ]
  },
  "actions": {                        // every command: the core availability rule
    "attach": { "visible": true, "enabled": false }, "detach": { ... }, "read": { ... },
    "start": { ... }, "stop": { ... }, "sync": { ... }, "test": { ... }, "erase": { ... },
    "download": { ... }, "open": { ... }, "save": { ... }, "reset": { ... }
  },
  "recent": [ { "path": "/abs/compass.json", "name": "compass.json" } ],
  "erase": { "value": 12, "max": 256, "text": "Erasing flash..." },   // or null
  "download": { "value": 96, "max": 512 },                          // or null
  "prompt": null                      // see Prompts
}
```

## Prompts

qtmonitor's modal dialogs are prompts: the engine queues them, the state
carries the first with a fresh `id`, and the page shows it once and answers
with `answer {id, button, index?, path?}`. An answer naming any other id is
ignored. Where qtmonitor continued after a dialog, the engine continues
after the answer.

| `kind` | Fields | Buttons |
| --- | --- | --- |
| `alert` | `title`, `text`, `informative`, `detail`, `style` (`warning`, `error`, `info`) | `ok` |
| `chooseBase` | `title`, `label`, `items[]` | `ok` with `index`, `cancel` |
| `startAnyway` | `title`, `text`, `failures[]` (the start checks' sentences) | `start`, `cancel` |
| `confirmErase` | `title`, `text` | `ok`, `cancel` |
| `unsaved` | `title`, `text`, `informative` | `save` (then a `saveFile`), `discard`, `cancel` |
| `openFile` | `title`, `start`, `filters` | `ok` with `path`, `cancel` |
| `saveFile` | `title`, `suggested`, `filters` | `ok` with `path`, `cancel` |
| `chooseTagType` | `title`, `label`, `items[]` (attached tag's type first) | `ok` with `index`, `cancel` |

The file dialogs are prompts, not the page's own, so the engine decides
where they start and what happens next: Save after Unsaved continues with
what was interrupted; Download adds `.db3` to a name with no extension.

## The form

The Configuration tab is drawn from `config.panels`, a list of the panels
the document's tag type shows (qtmonitor's panel table), each a `title` and
its `controls`. A control is

```jsonc
{ "id": "adxl.act", "kind": "number", "group": "Activity Detection",
  "label": "Active Threshold", "tooltip": "Wakeup threshold", "enabled": true,
  "value": 0.5, "min": 0.03, "max": 2, "step": 0.03125, "decimals": 2, "suffix": "g" }
```

with `kind` one of `radio` (`choices: [{id, text, tooltip}]`, `value` the
chosen id), `check` (`text`, `value` a bool), `number` (`min`, `max`,
`step`, `decimals`, `suffix`) or `datetime` (`value`, `min`, `max` in
seconds since the epoch, whole UTC hours). `group` names the box it sits in;
a group named as its panel is the panel itself. The page sends each edit as
`set_field {id, value}` and shows what the next state holds: the engine
clamps, rounds and applies the panels' rules (a range re-limits the
thresholds, a hibernation row first put in use starts at the logging start),
exactly as qtmonitor's widgets do. `core/src/model/configform.rs` is the
form; `tests/config_probe.rs` checks it against all of qtmonitor's probe
goldens, and `cargo run -p qtmon-core --example form_view -- BITTAG` prints
one tag type's panels.

## Commands

| `cmd` | `args` | Does |
| --- | --- | --- |
| `ready` | -- | the page is listening: resend the state; attach the first time |
| `attach`, `detach`, `read`, `start`, `stop`, `sync`, `test`, `erase`, `download`, `open`, `save`, `reset` | -- | the menu commands, by the core table's names; ignored unless `actions` enables them |
| `answer` | `{id, button, index?, path?}` | answers the shown prompt |
| `set_field` | `{id, value}` | one edit of a form control (see The form); ignored while the form is read-only |
| `new_config` | `{tagType}` | File ▸ New Configuration ▸ type (asks about unsaved changes) |
| `open_recent` | `{path}` | File ▸ Open Recent ▸ file |
| `set_approved` | `{on}` | the strip's Approved box |
| `hide_erase` | -- | the erase sheet's Close (the erase continues) |
| `cancel_download` | -- | the download sheet's Cancel (stops after the current page; keeps what was written) |
| `quit` | -- | File ▸ Quit and window close: asks about unsaved changes, detaches, then emits `Exit` |
| `set_log_level` | `{level}` | TRACE..ERROR |
| `save_text` | `{path, text}` | Save Error Log |

## Queries

| `name` | Returns |
| --- | --- |
| `menus` | `{menus: [{title, entries: [{kind: "command"\|"item"\|"separator", id, label, tooltip, shortcut}]}], newConfigurationTypes: [...]}`, from the core's menu table |
| `about` | `{title, text}` |
| `user_guide_url` | URL |
| `default_download_name` | the path Download suggests |

The page builds both its menu bar and its popup menu (the menu bar's menus
as submenus, on right-click over the Tag page, the strip and the
Configuration page) from `menus`, and reads `actions` and `recent` each time
a menu opens.

## Deliberate differences from qtmonitor

- The form's date and time boxes are the browser's, which show minutes
  (always :00) in the browser's own format, marked UTC.
- Download Data... needs the `sqlitelog` feature (the C++ SQLite writer
  through `taglogwriter`); without it the download refuses with a message.
- There is no fixture mode or screenshot capture; the browser mock serves
  for designing the page.
