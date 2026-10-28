---
type: readme
status: current
summary: The host UI kit - a macOS-native look in plain CSS, Preact components that draw it, and a small script for the engine connection, menus, dialogs and file pickers, shared by the host applications' web front ends.
---

# Host UI kit

A web front end for a host application is a page that draws a view state sent
by a Rust engine and turns clicks into commands. This kit is the part every
such page shares: a look that matches the Qt applications on macOS, and the
plumbing to talk to the engine, show dialogs and pick files. Tag Programmer
(`host/applications/tagprogram-rs/ui`) and Tag Monitor
(`host/applications/tagmonitor-rs/ui`) use it.

Pages render with [Preact](https://preactjs.com) through
[htm](https://github.com/developit/htm), which writes templates as tagged
strings, so there is still no npm and no build step: every file here is plain
CSS or a classic script, vendored or written by hand
([decision 0038](../../docs/decisions/0038-host-ui-kit-preact.md)).

| File | What it is |
| --- | --- |
| `kit.css` | Colour tokens (light and dark), window chrome, controls, group panels, status rows, key/value forms, dialogs and the log drawer |
| `preact-htm.js` | Preact 10.29.8, its hooks and htm 3.1.1, vendored (see [Updating Preact and htm](#updating-preact-and-htm)) |
| `kit.js` | Defines the global `Kit`: the engine connection, the menu bar, dialogs, file pickers, URL and folder opening |
| `kit-components.js` | Adds `Kit.html`, `Kit.render`, `Kit.hooks`, the components in `Kit.ui` and `Kit.logModel` |

## Using it in a page

```html
<link rel="stylesheet" href="…/ui-kit/kit.css">
<body>
  <div class="kit-window">
    <div class="kit-titlebar"><div class="kit-traffic"><span></span><span></span><span></span></div>
      <span class="kit-title"></span></div>
    <div class="kit-content"> <div id="menubar"></div> <div id="app" class="kit-root"></div> </div>
  </div>
  <script src="…/ui-kit/preact-htm.js"></script>
  <script src="…/ui-kit/kit.js"></script>
  <script src="…/ui-kit/kit-components.js"></script>
  <script src="mock-engine.js"></script>   <!-- defines MockEngine; unused in the app -->
  <script src="app.js"></script>
</body>
```

```js
const { html, render } = Kit;
const { Group, Row, Button } = Kit.ui;
const params = Kit.setupPage("My App");          // ?theme=, preview title bar
const engine = Kit.createEngine(window.MockEngine ? MockEngine.create(params) : null);
const Window = ({ s }) => html`
  <${Group} title="Tag"><div class="kit-form"><${Row} single label="UUID" value=${s.uuid}/></div><//>
  <${Button} kind="default" onClick=${() => engine.command("start")}>Start<//>`;
engine.on("state", (s) => render(html`<${Window} s=${s}/>`, document.getElementById("app")));
engine.command("start");
```

The scripts load in that order. `kit-root` (`display: contents`) lets the
rendered children lay out as children of `kit-content`.

The kit's scripts are classic scripts rather than ES modules on purpose. Browsers
refuse module imports from `file://`, and a page you can open straight from
disk, with no server, is what makes designing against a mock quick.

## Rendering with Preact

A page renders its whole window from each view state:
``render(html`<${Window} s=${state}/>`, root)``. Preact compares the new
tree with the old one and touches only what changed, so a field being typed
into keeps its focus and text. The page keeps only what is its own (an open
drawer, a selected tab) in plain variables and renders again when it
changes.

| Component | Draws |
| --- | --- |
| `Button({kind, small, onClick})` | `kit-button`; `kind` is `"default"` or `"destructive"` |
| `Disclosure({pressed, onClick})` | `kit-toolbutton` with its arrow, such as "Log" |
| `Select({options, value, onChange})` | `kit-select`; `options` are strings or `{value, text, disabled}`. It shows `value` after every render, even when the user picked an entry the page refused; `null` shows none |
| `Group({title, titleTooltip})` | `kit-group`: the title above a `kit-panel` holding the children |
| `Row({label, value, single})` | One `kit-form` row: the label cell and a selectable value cell; children replace `value` |
| `Progress({fraction})` | `kit-progress`, `fraction` 0 to 1, or null for busy |
| `StatusGrid({rows})` | `kit-status-grid`; each row `{level, name, text, buttons}` |
| `LogPane({log, level, onLevel, onSave, onClear})` | The `kit-log` drawer; Clear only with `onClear` |

Every component passes the attributes it does not use (`id`, `title`,
`hidden`, `class`) to its outer element. `Kit.logModel()` holds a log's text
outside the page's components, so the engine's `log` listener can be
registered before the first render; `LogPane` writes the text area directly,
so a long log costs nothing per render.

`Kit.dialog` takes a Preact element as its `body` as well as a Node. It is
rendered into the dialog and unmounted when the dialog closes. Tag
Programmer's Program dialog is an example: a component with the label field
and the notes that follow it.

## The engine connection

`Kit.createEngine(mock)` returns `{ kind, command, query, on }`:

- `command(cmd, args)` → `invoke("command", {cmd, args})`. The answer is
  `null`, or an error string.
- `query(name, args)` → `invoke("query", {name, args})`. The answer is JSON.
- `on(event, cb)` → `listen(event, …)`. Callbacks receive the payload. The
  call returns a promise, so await it before sending the first command, or
  the first state can be missed.

Inside the app (`window.__TAURI__` exists, `withGlobalTauri: true`) calls go
to Tauri. Otherwise they go to `mock`, which must implement the same three
functions. An application's contract document says what the commands, queries
and view state are. Tag Programmer's is
`host/applications/tagprogram-rs/design/ui-engine-contract.md`.

## Helpers

| Call | Does |
| --- | --- |
| `Kit.setupPage(title)` | Applies `?theme=dark\|light` and, in a browser, shows the stand-in title bar. Returns the `URLSearchParams`. |
| `Kit.setTitle(text)` | Sets the document title, the preview title bar and, in the app, the window title (which needs the `core:window:allow-set-title` permission). |
| `Kit.el(tag, attrs, ...children)` | Small element builder. `on*` attributes become listeners. |
| `Kit.menuBar(host, menus)` | An in-window menu bar, as the Qt apps use. `menus = [{title, items}]`, where an item is `{label, action, checked, disabled, tooltip}` or `"-"`. `items` can be a function, evaluated each time the menu opens, so check marks follow the state. An item with its own `items` is a submenu. |
| `Kit.contextMenu(ev, items)` | A popup menu at the pointer, from a `contextmenu` event, with items as for a menu (submenus included). |
| `Kit.dialog({title, body, buttons, width, onOpen})` | A modal sheet. `buttons` run left to right. Each is `{label, value, default, destructive, enabled, cancel, enter}`, or `"spacer"`. It resolves with the value of the button pressed. Escape picks the `cancel` button and Enter the `enter` one. After the call, `b.node` is each button's element, for enabling it live. |
| `Kit.alert(title, text, info)` | A message box with an OK button. |
| `Kit.confirm(title, text, info, buttons, width)` | A question box, with `buttons` as for `dialog`. |
| `Kit.promptText(title, label, initial, placeholder)` | A one-line text prompt. Resolves with the text, or `null`. |
| `Kit.chooseFromList(title, label, items, current)` | Pick one string. Resolves with its index, or `null`. |
| `Kit.openFile({title, filters, defaultPath})` / `Kit.saveFile(…)` | Native file dialogs through Tauri's dialog plugin. In a browser, a text prompt for the path. |
| `Kit.openUrl(url)` / `Kit.revealPath(path, title)` | Open a URL, or show a file in its folder, through Tauri's opener plugin. In a browser, a new tab, or an alert naming the path. |

Most dialog texts come from the engine. The few that a UI writes itself,
such as a confirmation, should copy the Qt application's wording exactly.

## Classes

| Class | Use |
| --- | --- |
| `kit-window`, `kit-content`, `kit-titlebar` | The window. `kit-content` has the Qt 12 px margins. The title bar shows only in a browser preview (`body.kit-preview`). |
| `kit-menubar` | Filled by `Kit.menuBar`. |
| `kit-button` (`.default`, `.destructive`, `.small`) | Push buttons. `.default` is the blue one. |
| `kit-select`, `kit-field` | A popup button and a text field. |
| `kit-toolbutton` + `kit-arrow` | A disclosure toggle, such as "Log". Set `aria-pressed`. |
| `kit-progress` (`.busy`) | A 200 px bar with an inner `<div>` for its width. `.busy` is indeterminate. |
| `kit-group` > `kit-group-title` + `kit-panel` | A Qt macOS group box: a small title above a rounded grey panel. |
| `kit-status-grid` | Status rows: `kit-dot` (`.ok` `.warn` `.fail` `.pending`), `kit-status-name`, `kit-status-text`, `kit-status-buttons`. |
| `kit-form` | A key/value form: `kit-form-label` (right-aligned) and `kit-form-value` (`.single` for one line). |
| `kit-text-ok`, `-fail`, `-warn`, `-secondary` | Text colours. |
| `kit-log` | The log drawer: a `<textarea>` and a `kit-log-bar`. |
| `kit-selectable` | Text the user can select. All other text is not, as in a native app. |

The status colours are qtprogram's: #2e7d32, #c77700, #c62828 and #9e9e9e,
lightened in dark mode.

## Themes

The colours are custom properties on `:root`. Dark values apply under
`prefers-color-scheme: dark`, and `html[data-theme="dark"|"light"]` forces
one theme or the other. Use the tokens (`--panel-bg`, `--text`, `--accent`,
`--ok`, …) in an application's own CSS, never literal colours, and dark mode
comes for free.

## Checking a page without the app

Open the page as a file, with headless Chrome for a screenshot:

```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless \
  --hide-scrollbars --force-device-scale-factor=1 --virtual-time-budget=1500 \
  --window-size=900,697 --screenshot=out.png "file://$PWD/index.html?theme=dark"
```

`--virtual-time-budget` lets the page's start-up promises run before the
capture.

## Shipping the kit with an app

A page refers to the kit by a relative path (`../../../ui-kit/` from
`tagprogram-rs/ui`). A Tauri app serves only its `frontendDist` directory, so
the shell must make the kit reachable from there. It can copy `host/ui-kit/`
into the bundled directory at build time, or point `frontendDist` at a
directory that holds both the kit and the page.

## Updating Preact and htm

`preact-htm.js` is the three published UMD builds, joined, with their
source-map comments removed. Its header names the files and versions. To
update, `npm pack preact@<version> htm@<version>` in an empty scratch
directory, unpack both, and rebuild the file from `preact/dist/preact.umd.js`,
`preact/hooks/dist/hooks.umd.js` and `htm/dist/htm.umd.js`, in that order.
Then update the versions in its header, in this README and in
`LICENSES/host/` (`README.md`, `third-party/preact.txt` and `htm.txt`).
