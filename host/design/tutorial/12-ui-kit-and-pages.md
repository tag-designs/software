---
type: design
status: current
summary: "A guided reading of the web side of the Tauri applications - the UI kit (kit.css, Preact and htm, kit.js, the shared components), Tag Monitor's page from boot to prompts, its mock engine and the headless-Chrome screenshots - for a reader who knows Qt and little modern front-end."
---

# 12. The page: the UI kit, Preact and htm

Everything the user sees in Tag Programmer, Tag Monitor and Tag Calibrator is
a web page that draws a JSON view state sent by the Rust engine and turns
clicks into named commands. This chapter reads the shared part,
[`host/ui-kit`](../../ui-kit/README.md) (`kit.css`, `preact-htm.js`,
`kit.js`, `kit-components.js`, `heading-dial.js`), and then Tag Monitor's page
([`applications/tagmonitor-rs/ui`](../../applications/tagmonitor-rs/ui/)) in
depth, ending with its mock engine and how the user manual's pictures are
taken. After reading it you can open `app.js`, follow a `state` event to the
pixels it changes, follow a click to the `invoke` that carries it to Rust, and
change the page against the mock in a browser with no app and no tag.

The Rust side of the same boundary is [chapter 10](10-tauri-shell.md) (the
shared shell, `invoke`, `emit`, the page staging) and [chapter 11](11-engine-and-model.md)
(the engine and the view state). The pattern as a whole is
[chapter 10 of the guide](../guide/10-application-pattern.md); why Preact was
chosen is [decision 0038](../../../docs/decisions/0038-host-ui-kit-preact.md).

## The picture, in Qt terms

| Qt original | Here |
| --- | --- |
| `QMainWindow` and its `.ui` form | `index.html` (window chrome only) and the `Window` component in `app.js` |
| QSS and the palette | `kit.css` custom properties (`--accent`, `--ok`, ...), redefined for dark mode |
| a `QWidget` subclass | a JavaScript function returning `` html`...` `` (a Preact component) |
| `setText()`/`setEnabled()` on each widget from each signal | render the whole window again from the state; Preact changes only the DOM that differs |
| a queued signal from a worker thread to the GUI thread | the Tauri event `state` and its listener |
| `QMessageBox::exec()` | `await Kit.alert(...)`: a promise, so nothing blocks |
| `QFileDialog::getSaveFileName()` | `await Kit.saveFile(...)` (Tauri's dialog plugin) |
| `QMenuBar` | `Kit.menuBar(host, menus)`: an HTML menu bar inside the window |
| `QProgressDialog` | a `Kit.dialog` whose body is re-rendered with `.update(body)` |

Two rules make the rest readable. The page holds **no rules**: which actions
are enabled, every text, the configuration form's controls and the menus all
come from the engine. And dialogs the engine needs are **data**: a `prompt`
object in the view state that the page shows once and answers with a command
([guide, "Prompts: dialogs as data"](../guide/10-application-pattern.md#prompts-dialogs-as-data)).

## Classic scripts, loaded in a fixed order

Start with the window's HTML. It is chrome and a list of scripts.
[tagmonitor-rs/ui/index.html:17](../../applications/tagmonitor-rs/ui/index.html#L17):

```html
<body>
  <div class="kit-window">
    <div class="kit-titlebar">
      <div class="kit-traffic"><span></span><span></span><span></span></div>
      <span class="kit-title">Tag Monitor</span>
    </div>
    <div class="kit-content">
      <div id="menubar"></div>

      <div id="app" class="kit-root"></div>
    </div>
  </div>

  <script src="../../../ui-kit/preact-htm.js"></script>
  <script src="../../../ui-kit/kit.js"></script>
  <script src="../../../ui-kit/kit-components.js"></script>
  <script src="mock-engine.js"></script>
  <script src="app.js"></script>
</body>
```

- `#menubar` receives the menu bar, built imperatively by `kit.js`.
  `#app` receives everything below it, rendered by Preact. `kit-root` is
  `display: contents` ([kit.css:366](../../ui-kit/kit.css#L366)), so what
  Preact renders lays out as if it were a direct child of `.kit-content`.
- `.kit-titlebar` is a stand-in title bar with fake traffic lights. It is
  `display: none` unless the body has the class `kit-preview`
  ([kit.css:152-164](../../ui-kit/kit.css#L152)), which the kit adds only in
  a plain browser, so the screenshots look like a window.
- The `<script>` elements are **classic scripts**: no `type="module"`. A
  browser runs them synchronously, in document order, after the elements
  above them exist. There is no import mechanism between them; each script
  adds names to the one global object (`window`) and the next one reads
  them. The order *is* the link order: `preact-htm.js` defines `preact`,
  `preactHooks` and `htm`; `kit.js` defines `Kit`; `kit-components.js` adds
  to `Kit`; `mock-engine.js` defines `MockEngine`; `app.js` uses them all.
- `mock-engine.js` is loaded in the shipped app too, but `app.js` constructs
  the mock only outside it (below).
- The `../../../ui-kit/` paths are relative to the source tree. The app
  crate's build script (through `tagapp-shell-build`) rewrites them to
  `ui-kit/` when it stages the page for embedding
  ([chapter 10](10-tauri-shell.md#buildrs-staging-the-page)).

**Why not ES modules?** A modern page would write `import { h } from
"./preact.js"`. Browsers refuse module imports from a `file://` URL, and the
whole design depends on opening `index.html` straight from disk against the
mock engine. The kit's README says it in one line
([ui-kit/README.md:64](../../ui-kit/README.md#L64)): "Browsers refuse module
imports from `file://`, and a page you can open straight from disk, with no
server, is what makes designing against a mock quick." The same constraint
pins three.js to r159 in Tag Calibrator ([chapter 14](14-the-three-applications.md)).
It also means no npm, no bundler and no build step anywhere: every file is
plain CSS or a script, vendored or written by hand.

Each script wraps its body the same way, for example
[kit.js:16](../../ui-kit/kit.js#L16):

```js
(function () {
  "use strict";
```

> **JavaScript:** `(function () { ... })();` defines a function and calls it
> at once (an "IIFE"). Its `const`s and functions are private to it, like
> names in an anonymous namespace in a `.cc` file; only what is assigned to
> `window.X` escapes. `"use strict"` turns several silent mistakes (assigning
> to an undeclared name, for one) into exceptions.

## The look: kit.css, tokens and dark mode

`kit.css` is the whole look of every page: colours, the window chrome, menus,
buttons, popups, fields, progress bars, group boxes, status rows, dialogs and
the log drawer, measured against qtprogram on macOS (13 px system font, 24 px
controls). Colours are **custom properties** ("tokens") on `:root`.
[kit.css:13](../../ui-kit/kit.css#L13):

```css
:root {
  --font: -apple-system, BlinkMacSystemFont, "Helvetica Neue", "Segoe UI", system-ui, sans-serif;
  --font-mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --font-size: 13px;
  --font-size-small: 11px;

  --win-bg: #ffffff;
  --panel-bg: #f7f7f7;
  --text: #1d1d1f;
```

and further down, [kit.css:37](../../ui-kit/kit.css#L37), the status colours
qtprogram uses:

```css
  --ok: #2e7d32;
  --warn: #c77700;
  --fail: #c62828;
  --pending: #9e9e9e;
```

Every rule after that says `var(--win-bg)`, `var(--accent)` and so on, never
a literal colour. Dark mode is then only a second set of values for the same
names. [kit.css:56](../../ui-kit/kit.css#L56):

```css
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --win-bg: #1e1e1e;
    --panel-bg: #2a2a2b;
    --text: #e8e8ea;
```

and again at [kit.css:92](../../ui-kit/kit.css#L92):

```css
:root[data-theme="dark"] {
  --win-bg: #1e1e1e;
  --panel-bg: #2a2a2b;
  --text: #e8e8ea;
```

The first block follows the operating system's setting unless the page forces
light; the second applies when the page forces dark. CSS cannot share one
block between a media query and an attribute selector, so the dark values are
written out twice. The forcing attribute is `<html data-theme="...">`, set by
`Kit.setupPage` from `?theme=` (below). Each block also sets `color-scheme`,
which tells the webview to draw its own parts (scroll bars, the date picker)
to match. A page that adds a colour does the same thing: Tag Calibrator's
`--view-bg` for its 3D views is defined the same three times
([tagcalibrate-rs/ui/app.css:9](../../applications/tagcalibrate-rs/ui/app.css#L9)).

The Qt equivalent is a palette with two sets of roles; the difference is that
here the switch needs no code at all.

## preact-htm.js: the rendering library

[ui-kit/preact-htm.js:1](../../ui-kit/preact-htm.js#L1) is three vendored
libraries in one file; read its header and nothing else:

```js
/*
 * Preact 10.29.8, its hooks, and htm 3.1.1: the UI kit's rendering library.
 * Vendored as their published UMD builds, unmodified but for their
 * source-map comments, and joined in this one classic script so a page still
 * opens from file:// (see README.md). It defines the globals `preact`,
 * `preactHooks` and `htm`; kit-components.js binds them into `Kit.html`,
 * `Kit.render` and `Kit.hooks`.
 *
 *   preact/dist/preact.umd.js        Preact 10.29.8   MIT, (c) 2015-present Jason Miller
 *   preact/hooks/dist/hooks.umd.js   Preact 10.29.8   MIT, (c) 2015-present Jason Miller
 *   htm/dist/htm.umd.js              htm 3.1.1        Apache-2.0, (c) 2018 Google Inc.
 *
 * Licenses: LICENSES/host/third-party/preact.txt and htm.txt. To update,
 * from an empty directory:
 *   npm pack preact@<v> htm@<v>, unpack each, and rebuild this file from the
 *   three files above, in that order, each after a line naming it.
 */
```

**Library: Preact.** A small (about 4 KB) re-implementation of React's
programming model. You describe what the window should look like as a tree of
lightweight objects, *virtual nodes*, built by `h(type, props, ...children)`:
`h("button", {class: "kit-button"}, "Start")` is a plain object saying "a
button with this class and this text", not a DOM element. `render(tree,
element)` makes the real DOM under `element` match the tree. Call `render`
again with a new tree and Preact compares it with the previous one ("diffing")
and changes only the DOM nodes that differ: an attribute here, a text there.
A *component* is a function from one `props` object to a tree; Preact calls it
while rendering.

**Library: preact/hooks.** Per-instance state and lifecycle for function
components: `useState`, `useEffect`, `useLayoutEffect`, `useRef`. The pages
use four of them, read below where they occur.

**Library: htm.** "Hyperscript Tagged Markup": it lets the code write the tree
in HTML-like syntax, `` html`<button class="kit-button">Start</button>` ``,
and turns that into `h(...)` calls at run time, so no JSX compiler (and so no
build step) is needed. `htm.bind(h)` makes the `html` function.

**UMD** ("Universal Module Definition") is a packaging convention: the file
checks whether it is being loaded by CommonJS, by AMD or as a plain
`<script>`, and in the last case assigns its exports to a global. That is the
`(n||self).preact={}` visible at the start of the first minified line.

## kit.js: the engine connection, and everything that is not Preact

`kit.js` defines the global `Kit` ([kit.js:560](../../ui-kit/kit.js#L560)):
the engine connection, `Kit.el`, the menu bar and popup menus, the dialogs,
the file pickers and URL opening. It predates the move to Preact, so its menus
and dialog frames are built with plain DOM calls; only a dialog's *body* may be
Preact. Two styles in one kit are therefore expected, not an accident.

### Kit.createEngine: Tauri or the mock

[kit.js:24](../../ui-kit/kit.js#L24):

```js
  const tauri = () => (window.__TAURI__ && window.__TAURI__.core ? window.__TAURI__ : null);
```

and [kit.js:51](../../ui-kit/kit.js#L51):

```js
  function createEngine(mock) {
    const t = tauri();
    if (t) {
      return {
        kind: "tauri",
        command: (cmd, args) => t.core.invoke("command", { cmd, args: args || {} }),
        query: (name, args) => t.core.invoke("query", { name, args: args || {} }),
        on: (event, cb) => t.event.listen(event, (e) => cb(e.payload)),
      };
    }
    if (!mock) throw new Error("Kit.createEngine: no Tauri and no mock engine");
    return Object.assign({ kind: "mock" }, mock);
  }
```

`window.__TAURI__` exists only inside the app, and only because each
`tauri.conf.json` sets `"withGlobalTauri": true`, which injects Tauri's
JavaScript API as a global object instead of an npm package a bundler would
have to resolve ([chapter 10](10-tauri-shell.md)). The kit uses
`__TAURI__.core.invoke`, `__TAURI__.event.listen`,
`__TAURI__.window.getCurrentWindow().setTitle`, and the plugins'
`__TAURI__.dialog` and `__TAURI__.opener`. The choice between Tauri and the
mock is that one test: no build flag, no URL parameter.

The returned object is the page's whole view of the engine:

- `command(cmd, args)` becomes `invoke("command", {cmd, args})`. Tauri matches
  the object's keys to the Rust function's parameter names: `fn command(engine:
  State<'_, Engine>, cmd: String, args: Value)`, where `engine` is injected by
  Tauri, not sent.
- `query(name, args)` becomes `invoke("query", {name, args})` and resolves
  with the JSON the engine answers.
- `on(event, cb)` registers a listener; Tauri delivers an event object and
  the kit passes on only its `.payload`.

All three return promises.

> **JavaScript:** `(cmd, args) => expr` is an *arrow function*, a lambda
> returning `expr`; with braces, `(e) => { ... }`, it has a body. It captures
> the variables around it by reference, as `[&]` would, but they live as long
> as the function does (a *closure*), so `t` stays usable after
> `createEngine` returns. `{ cmd, args: args || {} }` is an object literal;
> `{ cmd }` is shorthand for `{ cmd: cmd }`, and `args || {}` means "`args`,
> or an empty object if `args` is missing".

**`command` returns nothing from Rust.** As the doc comment above
`createEngine` says ([kit.js:40](../../ui-kit/kit.js#L40)), `command`
"resolves with null (the Rust command returns nothing) and rejects only if
the IPC call itself fails". The shared shell's `fn command(...)` has no
return value
([tagapp-shell-rs/src/lib.rs:153](../../libraries/tagapp-shell-rs/src/lib.rs#L153)),
so an unknown or refused command is reported by the engine as a log line or
an `alert` prompt in the next state. The page's `send()` (below) alerts on a
non-null answer or a rejected `invoke`; in practice only the second can
happen, and only when Tauri itself fails the call (for example, arguments
that do not deserialise). Read the alert in `send()` as a guard for
transport failures, not as the path by which the engine reports errors.

`Object.assign({ kind: "mock" }, mock)` copies the mock's own properties onto
a new object, so a mock can carry extras (Tag Programmer's has
`setScenario`).

### Kit.el and Kit.setupPage

[kit.js:81](../../ui-kit/kit.js#L81) is a ten-line DOM builder used by the
menus, the dialogs and the mocks:

```js
  function el(tag, attrs, ...children) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v === undefined || v === null || v === false) continue;
      if (k.startsWith("on") && typeof v === "function") node.addEventListener(k.slice(2), v);
      else if (k === "class") node.className = v;
      else if (k === "text") node.textContent = v;
      else if (k === "style" && typeof v === "object") Object.assign(node.style, v);
      else if (v === true) node.setAttribute(k, "");
      else node.setAttribute(k, v);
    }
    for (const c of children.flat()) {
      if (c === null || c === undefined || c === false) continue;
      node.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
    return node;
  }
```

> **JavaScript:** `...children` in a parameter list is a *rest parameter*:
> every argument after the second, collected into an array (roughly a C
> variadic `...` that arrives as a vector; JavaScript arrays are untyped). `for (const [k, v] of
> Object.entries(o))` iterates an object's key/value pairs and unpacks each
> pair, like `for (auto& [k, v] : map)`. `===` compares without type
> conversion; the code never uses `==`.

Note the lower-case `onclick`: `Kit.el` passes `on*` attributes to
`addEventListener` after stripping `on`. Preact templates use `onClick`.

`setupPage` is called once by every page. [kit.js:109](../../ui-kit/kit.js#L109):

```js
  function setupPage(title) {
    const params = new URLSearchParams(location.search);
    const theme = params.get("theme");
    if (theme === "dark" || theme === "light") document.documentElement.dataset.theme = theme;
    if (!inApp()) document.body.classList.add("kit-preview");
    if (title) setTitle(title);
    return params;
  }
```

It applies `?theme=`, shows the stand-in title bar in a browser, and returns
the query parameters for the page and its mock. `setTitle`
([kit.js:122](../../ui-kit/kit.js#L122)) also sets the native window's title
through Tauri, which needs the `core:window:allow-set-title` permission; a
refusal is swallowed by `.catch(() => {})`.

### Menus

`Kit.menuBar(host, menus)` ([kit.js:160](../../ui-kit/kit.js#L160)) builds an
in-window menu bar from `[{title, items}]`, where `items` is a list of
`{label, action, checked, disabled, tooltip}` or `"-"` for a separator, or a
**function** returning that list. The function is the important part. When a
menu opens, [kit.js:183](../../ui-kit/kit.js#L183):

```js
            fill(drop, typeof menu.items === "function" ? menu.items() : menu.items, close);
```

so a menu built once can still show check marks and enabled states from the
latest view state, the way a Qt application updates its `QAction`s in
`aboutToShow`. Menus open on mouse-down and track the pointer across titles;
an item fires on mouse-up ([kit.js:232](../../ui-kit/kit.js#L232)). An item
with its own `items` is a submenu, filled when its menu opens and shown by CSS
on hover. `Kit.contextMenu(ev, items)` ([kit.js:253](../../ui-kit/kit.js#L253))
draws the same kind of list at the pointer for a right-click.

The menu bar is inside the window rather than the macOS global one because the
Qt applications it replaces draw theirs inside the window too.

### Kit.dialog: one modal primitive, returning a promise

Every dialog in the three apps is `Kit.dialog` or built on it.
[kit.js:311](../../ui-kit/kit.js#L311), with the button loop and key handling
elided:

```js
  function dialog({ title, body, buttons, width, onOpen }) {
    let finish;
    const promise = new Promise((resolve) => { finish = resolve; });
    const buttonNodes = [];
    const box = el("div", { class: "kit-dialog", role: "dialog", "aria-modal": "true",
      style: width ? { width: width + "px" } : undefined });
    const backdrop = el("div", { class: "kit-backdrop" }, box);
    if (title) box.append(el("div", { class: "kit-dialog-title" }, title));
    const content = el("div", { class: "kit-dialog-body" });
    const vnode = !(body instanceof Node);
    if (vnode) window.preact.render(body, content);
    else content.append(body);
    // ...
    function done(value) {
      document.removeEventListener("keydown", onKey, true);
      backdrop.remove();
      if (vnode) window.preact.render(null, content);
      finish(value);
    }
    // ...
    document.addEventListener("keydown", onKey, true);
    document.body.append(backdrop);
    if (onOpen) onOpen(box);
    const focus = box.querySelector("input, select, textarea") || buttonNodes.find((n) => n.classList.contains("default"));
    if (focus) setTimeout(() => focus.focus(), 0);
    promise.dialog = box;
    promise.close = done;
    promise.update = (next) => { if (vnode) window.preact.render(next, content); };
    promise.buttons = buttons;
    return promise;
  }
```

What it does: it draws a box over a translucent backdrop in the page (not a
native window), and returns a promise that resolves with the `value` of the
button pressed. Buttons are `{label, value, default, destructive, enabled,
cancel, enter}` or the string `"spacer"`. `default` only styles a button blue;
`enter: true` is what makes Return press it; `cancel: true` (or `value:
null`) is what Escape presses ([kit.js:343-352](../../ui-kit/kit.js#L343)).
Each button object gains a `.node` property, its DOM element, so a caller can
enable or disable it while the dialog is open.

> **JavaScript:** A `Promise` is a future: an object that will later hold a
> value (or an error). `new Promise((resolve) => { finish = resolve; })`
> keeps the function that settles it in `finish`, so a click much later can
> call `finish(value)`; this is the "deferred" idiom, like keeping the
> `std::promise` and handing out the `std::future`. A promise is an ordinary
> object, so the code hangs `.close`, `.update`, `.dialog` and `.buttons` on
> it as extra properties.

> **JavaScript:** `await` (below, in the callers) suspends only the
> `async` function that wrote it, until the promise settles; the event loop
> keeps running. So `const v = await Kit.dialog(...)` reads like
> `QDialog::exec()`, but while the dialog is up, `state` events keep arriving
> and the window under it keeps redrawing. Nothing in the page ever blocks.

Other details worth knowing:

- `body` may be a DOM node or a Preact tree. A tree is rendered into the
  dialog with `window.preact.render` (the raw global: `kit.js` loads before
  `kit-components.js` has made `Kit.render`), and unmounted with
  `render(null, content)` on close, which runs its effects' clean-ups.
- `.update(next)` renders a new body into the open dialog. Tag Monitor's
  progress sheets use it to follow the state.
- `document.addEventListener("keydown", onKey, true)`: the `true` registers
  in the *capture* phase, so the dialog sees keys before anything in the page.
- `setTimeout(() => focus.focus(), 0)` defers the focus to after the current
  task, when the box is attached and laid out.

Built on it, all in `kit.js`:

| Function | Draws | Resolves with |
| --- | --- | --- |
| `alert(title, text, info, style)` ([389](../../ui-kit/kit.js#L389)) | message box, OK | `true`; `style` is accepted but ignored |
| `confirm(title, text, info, buttons, width)` ([412](../../ui-kit/kit.js#L412)) | message box, caller's buttons | the button's value |
| `promptText(title, label, initial, placeholder)` ([427](../../ui-kit/kit.js#L427)) | one-line field | the text, or `null` |
| `chooseFromList(title, label, items, current)` ([456](../../ui-kit/kit.js#L456)) | list, widened to the longest row | the index, or `null` |

### Files and URLs

[kit.js:503](../../ui-kit/kit.js#L503):

```js
  async function openFile({ title, filters, defaultPath } = {}) {
    const d = dialogPlugin();
    if (d) {
      const path = await d.open({ title, filters, defaultPath, multiple: false, directory: false });
      return Array.isArray(path) ? path[0] || null : path || null;
    }
    return promptText(title || "Open", "Path of the file:", defaultPath || "", "/path/to/file");
  }
```

Inside the app this is the native open panel of Tauri's dialog plugin; in a
browser it falls back to asking for the path as text, so the mock flows can
still be clicked through. `saveFile` ([kit.js:521](../../ui-kit/kit.js#L521))
is the same over the native save panel, which also confirms overwriting.
Neither reads or writes a file: the page has no file access, and the engine
does the I/O (Tag Monitor's Save Error Log sends `save_text {path, text}`).

> **JavaScript:** `async function` always returns a promise; inside it,
> `await` unwraps one. `{ title, filters, defaultPath } = {}` destructures
> the single options argument into three locals, and the `= {}` default lets
> a caller write `openFile()` with no argument at all.

`openUrl(url)` ([kit.js:533](../../ui-kit/kit.js#L533)) opens a URL with the
opener plugin (the user manual, release notes) or `window.open` in a browser;
`revealPath(path, title)` ([kit.js:550](../../ui-kit/kit.js#L550)) shows a
file in Finder or Explorer (Tag Programmer's Show CSV).

## kit-components.js: Preact for a Qt programmer

[kit-components.js:24](../../ui-kit/kit-components.js#L24) binds the
libraries:

```js
  const { h, render, Fragment } = window.preact;
  const hooks = window.preactHooks;
  const { useEffect, useLayoutEffect, useRef } = hooks;
  const html = window.htm.bind(h);
```

> **JavaScript:** `const { h, render, Fragment } = window.preact;` is
> *object destructuring*: three locals initialised from the properties of the
> same names, like a structured binding by name. Every page opens the same
> way (`const { html, render, Fragment } = Kit;`).

### Templates: the `html` tag

`` html`<div class="x">${text}</div>` `` is not a string. It is a call.

> **JavaScript:** Backquotes make a *template literal*; `${expr}` inside it
> interpolates, like `std::format`. Put a function name directly before it,
> `` html`...` ``, and it becomes a *tagged template*: the function is called
> with the literal pieces (`['<div class="x">', '</div>']`) and the values
> (`[text]`) separately, instead of one joined string. htm parses the pieces
> once per call site (it caches them) and returns `h("div", {class: "x"},
> text)`, a virtual node. Values are never parsed as HTML, so a value
> containing `<script>` (a file name, say) is shown as text: there is no
> injection risk, unlike `innerHTML`.

The htm syntax the pages use:

| In a template | Means |
| --- | --- |
| `<${Group} title="Tag">...<//>` | a component as an element: the tag is a substitution holding the function; `<//>` closes whatever is open |
| `<${Row} single label="UUID" value=${t.uuid}/>` | self-closing; `single` alone means `true` |
| `onClick=${fn}`, `onChange=${fn}`, `onInput=${fn}` | event listeners (Preact's capitalisation) |
| `hidden=${bool}`, `disabled=${bool}`, `checked=${bool}` | boolean DOM properties; `false` removes them |
| `style=${{ width }}` | a style object (outer braces: substitution; inner: an object) |
| `ref=${ref}` | Preact stores the real DOM element in `ref.current` |
| `key=${id}` | identity of a list item, so the diff matches the right element across renders |
| `${array}` | a list of nodes; `false` and `null` render nothing, so `` ${cond && html`...`} `` is conditional content |
| `...${rest}` | spreads an object of props onto the element |

### Components are functions of props

[kit-components.js:47](../../ui-kit/kit-components.js#L47) (doc comment
dropped):

```js
  function Button({ kind, small, class: cls, children, ...rest }) {
    return html`<button type="button" class=${cx("kit-button", kind, small && "small", cls)} ...${rest}>${children}</button>`;
  }
```

`<${Button} kind="default" onClick=${go}>Start<//>` calls `Button` with one
object, `{kind: "default", onClick: go, children: "Start"}`.

> **JavaScript:** In the parameter list, `{ kind, small, class: cls,
> children, ...rest }` destructures that object: `kind` and `small` by name;
> `class` renamed to `cls` (`class` is a reserved word and cannot be a
> variable); `children` is what was written between the tags; `...rest`
> collects every other property into a new object. In the template,
> `...${rest}` spreads those back onto the `<button>`. Every kit component
> follows this rule, "take what you use, pass on the rest"
> ([kit-components.js:16](../../ui-kit/kit-components.js#L16)), so `id`,
> `title`, `hidden`, `disabled` and `onClick` reach the DOM element without
> the component naming them.

`cx` ([kit-components.js:34](../../ui-kit/kit-components.js#L34)) joins class
names, dropping falsy ones: `const cx = (...names) =>
names.filter(Boolean).join(" ");`. So `small && "small"` contributes `"small"`
or `false`.

> **JavaScript:** `a && b` yields `a` if `a` is falsy, else `b`; `a || b`
> yields `a` if truthy, else `b`. The code uses both as compact `if` and
> "default". Falsy values are `false`, `0`, `""`, `null`, `undefined` and
> `NaN`, which is why the code compares with `=== null` where `0` is a
> legitimate value.

The other components, each a few lines:

| Component | Line | Draws |
| --- | --- | --- |
| `Disclosure({pressed})` | [60](../../ui-kit/kit-components.js#L60) | a tool button with a turning arrow ("Log" in Tag Programmer) |
| `Select({options, value, onChange})` | [79](../../ui-kit/kit-components.js#L79) | a popup button that always shows `value` (below) |
| `Group({title, tooltip, titleTooltip})` | [103](../../ui-kit/kit-components.js#L103) | a Qt macOS group box: small title over a rounded panel |
| `Row({label, value, single})` | [119](../../ui-kit/kit-components.js#L119) | two grid cells of a `kit-form`: label and selectable value |
| `Progress({fraction})` | [131](../../ui-kit/kit-components.js#L131) | a bar; `fraction` null or undefined is the animated busy bar |
| `StatusGrid({rows})` | [146](../../ui-kit/kit-components.js#L146) | rows of dot, name, text and buttons (Tag Programmer's Station) |
| `LogPane({log, level, ...})` | [203](../../ui-kit/kit-components.js#L203) | the log text area and its bar (below) |

`Row` returns *two* sibling elements; that is allowed, and lets a page write
a label/value grid as a list of `Row`s inside one `div class="kit-form"`.
`StatusGrid` shows `key` and `Fragment` together,
[kit-components.js:146](../../ui-kit/kit-components.js#L146):

```js
  function StatusGrid({ rows, class: cls, ...rest }) {
    return html`<div class=${cx("kit-status-grid", cls)} ...${rest}>
      ${rows.map((r) => html`<${Fragment} key=${r.name}>
        <span class=${cx("kit-dot", r.level)}>●</span>
        <span class="kit-status-name">${r.name}</span>
        <span class="kit-status-text kit-selectable" title=${r.text}>${r.text || "—"}</span>
        <div class="kit-status-buttons">${r.buttons}</div>
      <//>`)}
    </div>`;
  }
```

`rows.map(...)` turns each row object into a group of four grid cells; a
`Fragment` groups them without adding an element, and its `key` lets Preact
match each row to its old DOM even if rows were inserted.

### Rendering the whole window, and why that is cheap

A page renders *everything* from each state:
``render(html`<${Window} s=${state}/>`, root)``. To a Qt programmer that sounds
like rebuilding every widget on every signal. It is not, because the tree is
only a description: Preact walks the new description beside the old one and
touches the DOM only where they differ. A label whose text did not change is
not written; a field with focus keeps its focus and its caret; a `textarea`
keeps its selection.

Compare it with QML. A QML binding (`text: model.state`) re-evaluates one
property when its dependency signals. Here there are no per-property
dependencies: the whole function runs again, and the diff finds the effect.
The engine already sends the whole state after each change
([chapter 11](11-engine-and-model.md)), so "redraw everything from the state"
fits it exactly, and there is no place where the page can forget to update a
widget. Decision 0038 records that it replaced about 500 lines of
hand-written DOM synchronisation per application.

What the page keeps for itself (the selected tab, the log level, whether the
log drawer is open) lives in plain variables. When one changes, the page calls
its `draw()` again. There is no framework store and almost no component
state: this is closer to an immediate-mode GUI than to Qt's retained widgets.

### Hooks: four of them, used sparingly

A *hook* is a function a component calls while it renders; Preact ties the
result to "the component instance currently rendering" by the order of the
calls (so hooks are never called conditionally). The four the pages use:

| Hook | Meaning | C++ analogue |
| --- | --- | --- |
| `useRef(init)` | the same `{current}` box on every render of this instance | a member variable |
| `useEffect(fn, deps)` | run `fn` after the DOM is updated and painted, when any value in `deps` changed; `[]` means once, at mount; no `deps` means after every render. If `fn` returns a function, that is the clean-up, run before the next run and at unmount | a constructor/destructor pair, or a slot connected to selected changes |
| `useLayoutEffect(fn, deps)` | the same, but synchronously after the DOM update and before the browser paints | as above, without a visible frame in between |
| `useState(init)` | `[value, setValue]`; calling `setValue` re-renders this component | a member plus `update()` |

`deps` are compared element by element with identity (`Object.is`). That
matters with JSON from the engine: every `state` event is freshly parsed, so
an object or array prop is a new object every time even if its contents are
the same.

`Select` shows the commonest use, writing a DOM property the diff would not.
[kit-components.js:79](../../ui-kit/kit-components.js#L79):

```js
  function Select({ options, value, onChange, class: cls, ...rest }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
      if (value === null || value === undefined) ref.current.selectedIndex = -1;
      else ref.current.value = value;
    });
    return html`<select ref=${ref} class=${cx("kit-select", cls)} ...${rest}
        onChange=${(ev) => onChange && onChange(ev.currentTarget.value)}>
      ${options.map((o) => (typeof o === "string"
        ? html`<option value=${o}>${o}</option>`
        : html`<option value=${o.value} disabled=${!!o.disabled}>${o.text}</option>`))}
    </select>`;
  }
```

The effect has no `deps`, so it runs after every render and puts the
engine's `value` back into the popup. If the user picks an entry the engine
refuses, the next render snaps the popup back to what the engine holds,
exactly as a Qt combo box driven from a model would. Tag Programmer relies on
this to undo a cancelled "Other ELF file..." (it just calls `draw()`).

> **JavaScript:** `!!x` converts any value to a real boolean, so
> `disabled=${!!o.disabled}` is `false`, not `undefined`, when the field is
> missing.

### The log: a model outside Preact, a pane that writes the DOM itself

[kit-components.js:166](../../ui-kit/kit-components.js#L166):

```js
  function logModel() {
    let text = "";
    const subscribers = new Set();
    return {
      append(line) {
        text += line + "\n";
        for (const f of subscribers) f(line + "\n");
      },
      clear() {
        text = "";
        for (const f of subscribers) f(null);
      },
      text: () => text,
      subscribe(f) {
        subscribers.add(f);
        return () => subscribers.delete(f);
      },
    };
  }
```

> **JavaScript:** No class: `text` and `subscribers` are locals of
> `logModel`, and the returned object's methods close over them, so they
> live as long as the object and nothing else can reach them. This is the
> idiom the pages use instead of private members. `subscribe` returns a
> function that undoes it, like a `QMetaObject::Connection` you pass to
> `disconnect`.

The page creates one model at load (`const log = Kit.logModel();`) and feeds
it from the `log` event before anything is rendered, so no line is lost and
the text survives the pane being hidden. The pane,
[kit-components.js:203](../../ui-kit/kit-components.js#L203):

```js
  function LogPane({ log, level, onLevel, onSave, onClear, class: cls, ...rest }) {
    const ref = useRef(null);
    useEffect(() => {
      const area = ref.current;
      area.value = log.text();
      return log.subscribe((chunk) => {
        if (chunk === null) { area.value = ""; return; }
        const atEnd = area.scrollTop + area.clientHeight >= area.scrollHeight - 4;
        area.value += chunk;
        if (atEnd) area.scrollTop = area.scrollHeight;
      });
    }, [log]);
```

The `<textarea>` is rendered empty and never given its text through Preact:
the effect fills it at mount and appends each new chunk directly, and its
return value (the unsubscribe function) is the clean-up. So a long log costs
nothing per render, keeps its selection, and follows new lines only while the
user is scrolled to the end. The rest of `LogPane` draws the bar: Save Error
Log, an optional Clear (`` ${onClear && html`...`} ``) and a `Select` of
`Kit.LEVELS`.

### heading-dial.js

The one SVG component, used by Tag Calibrator and Tag View.
[heading-dial.js:43](../../ui-kit/heading-dial.js#L43):

```js
  function HeadingDial({ heading }) {
    return html`<svg class="dial" viewBox="-125 -125 250 250" role="img" aria-label="Heading indicator">
      <defs>
        <radialGradient id="bezel" cx="40%" cy="35%" r="70%">
          <stop offset="0" stop-color="#6b6f76"/><stop offset="1" stop-color="#1c1e22"/>
        </radialGradient>
      </defs>
      <circle r="123" fill="url(#bezel)"/>
      <circle r="114" fill="#0d0d0d"/>
      <g transform=${`rotate(${-(heading || 0)})`}>${CARD}</g>
      ${MARKS}
      <path d="M0,-46 L5,-36 L5,-10 L38,8 L38,16 L5,8 L5,28 L15,36 L15,42 L0,38 L-15,42 L-15,36 L-5,28 L-5,8 L-38,16 L-38,8 L-5,-10 L-5,-36 Z"
        fill="none" stroke="#fff" stroke-width="2" stroke-linejoin="round"/>
    </svg>`;
  }
```

It is a port of sensorui's `MyCompass.qml`. The `viewBox` puts the origin at
the centre, so `rotate(d)` turns about the centre, and the SVG scales to its
CSS box. The card (`CARD`: ticks every 5 degrees, figures every 30) and the
bezel marks (`MARKS`) are built once at load as arrays of virtual nodes
([heading-dial.js:16-33](../../ui-kit/heading-dial.js#L16)) and reused in every
render; only the `<g>`'s transform changes. Its colours are literal: the dial
is dark in both themes, as the QML one is.

> **JavaScript:** `` transform=${`rotate(${-(heading || 0)})`} `` is an
> ordinary template literal inside an htm substitution: it builds the string
> `"rotate(-123.4)"`. `heading || 0` turns a `null` heading (no data) into 0.

## Tag Monitor's page

[tagmonitor-rs/ui/app.js](../../applications/tagmonitor-rs/ui/app.js) is
535 lines and draws the whole window: tabs, the status strip, the generated
Configuration page, Tag Info, the Error Log, progress sheets, prompts and
menus. Its contract with the engine is
[design/ui-engine-contract.md](../../applications/tagmonitor-rs/design/ui-engine-contract.md);
keep it open beside the code.

### Boot

[app.js:22](../../applications/tagmonitor-rs/ui/app.js#L22):

```js
(function () {
  "use strict";

  const { html, render, Fragment } = Kit;
  const { useLayoutEffect, useRef } = Kit.hooks;
  const { Button, Group, Progress, LogPane } = Kit.ui;
  const params = Kit.setupPage("Tag Monitor");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);

  let state = null;
  let menus = null;            // the `menus` query's answer
  let shownPrompt = null;      // the id of the prompt on screen or answered
  let eraseSheet = null;       // the open erase progress dialog
  let downloadSheet = null;    // the open download progress dialog
```

The mock is constructed only outside the app (`!Kit.inApp() && ...`). That
matters: constructing Tag Monitor's mock appends its scenario switcher to the
page. Then the page-local state ([app.js:41](../../applications/tagmonitor-rs/ui/app.js#L41)):

```js
  const ui = {
    page: ["tag", "config", "log"].includes(params.get("page")) ? params.get("page") : "config",
    level: "INFO",
  };
  const log = Kit.logModel();
```

`state` is the last view state; `ui` is what the page owns. Nothing else is
stored.

The wiring at the end of the file runs last.
[app.js:523](../../applications/tagmonitor-rs/ui/app.js#L523):

```js
  Promise.all([
    engine.query("menus").then((m) => { menus = m; Kit.menuBar(document.getElementById("menubar"), kitMenus()); }),
    engine.on("log", (e) => log.append(`${e.level} ${e.text}`)),
    engine.on("state", (s) => {
      state = s;
      Kit.setTitle(s.title);
      draw();
      renderProgress(s);
      showPrompt(s.prompt);
    }),
  ]).then(() => send("set_log_level", { level: ui.level }))
    .then(() => send("ready"));
})();
```

In order:

1. Ask the engine for its menu table and build the menu bar from it.
2. Register the two listeners. In Tauri, `listen` is asynchronous: it
   registers with the Rust side and resolves when that is done.
3. Only when all three promises have resolved, send `set_log_level`, then
   `ready`. Sending `ready` earlier could lose the first state, which the
   engine sends in answer to it. The kit README says the same:
   "await it before sending the first command, or the first state can be
   missed".

`ready` is the page saying "I am listening". The engine answers with the
whole state, and the first `ready` attaches to a tag as qtmonitor does when
its window opens (contract, [Transport](../../applications/tagmonitor-rs/design/ui-engine-contract.md#transport)).
Until the first `state` arrives, `draw()` renders nothing, so the window is
only the menu bar.

> **JavaScript:** `Promise.all([p1, p2, p3])` is a promise that resolves when
> all of them have, like waiting on several futures. `.then(f)` registers
> what to do next and returns a new promise, so `.then(...).then(...)`
> sequences steps. `` `${e.level} ${e.text}` `` is a template literal.

Each `state` event does four things: remember the state, set the window
title, redraw, and let the progress sheets and the prompt follow it. Note that
`showPrompt` is `async` and is not awaited here: the listener returns at once
while a dialog may stay open.

### send and answer

[app.js:54](../../applications/tagmonitor-rs/ui/app.js#L54):

```js
  const send = (cmd, args) =>
    Promise.resolve(engine.command(cmd, args)).then((err) => {
      if (err) Kit.alert("Tag Monitor", String(err), null, "warning");
    }, (err) => Kit.alert("Tag Monitor", String(err), null, "warning"));
```

and [app.js:65](../../applications/tagmonitor-rs/ui/app.js#L65):

```js
  const answer = (id, button, extra) => send("answer", Object.assign({ id, button }, extra || {}));
```

`Promise.resolve(x)` wraps a value that may or may not already be a promise,
so `send` works with a mock whose `command` answers synchronously. `.then(ok,
fail)` takes two callbacks: the first gets the resolved value, the second the
rejection. As the reading note above says, the first alert never fires in the
app, because `command` resolves with `null`; the second fires only if Tauri
fails the call. The page never learns a command's outcome from `send`: it
learns it from the next `state`.

`answer` merges `{id, button}` with optional extras (`index` for a list
choice, `path` for a file) into the `answer` command's arguments.

### draw and Window

[app.js:70](../../applications/tagmonitor-rs/ui/app.js#L70):

```js
  function draw() {
    if (state) render(html`<${Window} s=${state}/>`, document.getElementById("app"));
  }
```

That is the whole render path. It runs on every `state` event and whenever
page-local state changes. [app.js:88](../../applications/tagmonitor-rs/ui/app.js#L88):

```js
  function Window({ s }) {
    return html`
      <div class="tabs" role="tablist">
        ${TABS.map(([page, label, tooltip]) => html`<button type="button" role="tab" title=${tooltip}
          aria-selected=${String(ui.page === page)} onClick=${() => { ui.page = page; draw(); }}>${label}</button>`)}
      </div>
      <${Strip} s=${s.strip} actions=${s.actions}/>
      <div class="pages">
        <section id="page-config" class="page" hidden=${ui.page !== "config"} onContextMenu=${popup}>
          <${ConfigPage} c=${s.config}/>
        </section>
        <section id="page-tag" class="page" hidden=${ui.page !== "tag"} onContextMenu=${popup}>
          <${TagPage} t=${s.tag}/>
        </section>
        <section id="page-log" class="page" hidden=${ui.page !== "log"}>
          <${LogPane} class="log-page" log=${log} level=${ui.level} onLevel=${setLevel} onSave=${saveLog}
            onClear=${() => log.clear()}/>
        </section>
      </div>`;
  }
```

- The tab bar is three buttons; a click changes `ui.page` and calls `draw()`.
  No `useState`: the page re-renders itself from the top, by hand.
- All three pages stay mounted and the inactive ones are `hidden`, so a page
  keeps its scroll position and the log its text. A `QStackedWidget` behaves
  the same way.
- Each page component receives only its slice of the state (`s.config`,
  `s.tag`), which keeps the components readable.

> **JavaScript:** `TABS.map(([page, label, tooltip]) => ...)` maps each
> three-element array in `TABS` ([app.js:75](../../applications/tagmonitor-rs/ui/app.js#L75))
> to a button, unpacking it positionally (*array destructuring*, like
> `auto [page, label, tooltip] = row;`). `.map` returning nodes is how every
> list in the pages is drawn.

The status strip, `Strip` ([app.js:127](../../applications/tagmonitor-rs/ui/app.js#L127)),
shows the strip values with a warning colour when the engine sets `warn`, the
Approved check box and the Start and Stop buttons, whose visibility and
enabling come from `actions`. Its last lines,
[app.js:141](../../applications/tagmonitor-rs/ui/app.js#L141):

```js
      <label class="approved" title="Tick when you have reviewed the configuration. Start warns if it is not ticked; any change to the configuration clears it.">
        <input id="approved" type="checkbox" checked=${s.approved} disabled=${!s.approvalEnabled}
          onClick=${(ev) => send("set_approved", { on: ev.currentTarget.checked })}/> Approved
      </label>
      ${["start", "stop"].map((cmd) => html`<${Button} small hidden=${!actions[cmd].visible}
        disabled=${!actions[cmd].enabled} onClick=${() => send(cmd)}>${cmd === "start" ? "Start" : "Stop"}<//>`)}
```

`checked=${s.approved}` is written on every render, so a click the engine
refuses (or a configuration edit that clears approval) shows the engine's
value at the next state. `TagPage` ([app.js:186](../../applications/tagmonitor-rs/ui/app.js#L186))
draws the identity rows from `INFO_ROWS` and two count boxes; the
External Data box is hidden when `externalCount` is `null`.

### The configuration form is generated from data

There is no per-tag-type form in the page. The engine sends `config.panels`,
each `{name, title, controls}`, each control `{id, kind, group, label,
tooltip, enabled, value, ...}` with `kind` one of `radio`, `check`, `number`
or `datetime` (contract, [The form](../../applications/tagmonitor-rs/design/ui-engine-contract.md#the-form)).
The page draws by kind and sends every edit back as `set_field {id, value}`;
the engine applies qtmonitor's rules and the next state shows the value it
kept. [app.js:223](../../applications/tagmonitor-rs/ui/app.js#L223):

```js
  function ConfigPage({ c }) {
    return html`
      <div class="config-none" hidden=${c.document}>
        No configuration.<br/><br/>Use File ▸ New Configuration for a tag type's defaults, File ▸ Open
        Configuration for a saved file, or attach a tag to read its configuration.
      </div>
      <div class="config-doc" hidden=${!c.document}>
        <div class="config-head">
          <span class="kit-selectable">${c.name}</span>
          <span class="kit-text-warn">${c.modified ? "unsaved changes" : ""}</span>
        </div>
        <div class="config-panels">${c.panels.map((p) => html`<${Panel} key=${p.name} p=${p}/>`)}</div>
      </div>`;
  }
```

`key=${p.name}` matters when the tag type changes: Preact then replaces the
panels whose names changed instead of patching one panel's controls into
another's. A panel groups its controls by their `group` string.
[app.js:244](../../applications/tagmonitor-rs/ui/app.js#L244):

```js
  function Panel({ p }) {
    const byGroup = new Map();
    for (const k of p.controls) {
      if (!byGroup.has(k.group)) byGroup.set(k.group, []);
      byGroup.get(k.group).push(k);
    }
    const groups = [...byGroup].map(([title, controls]) => {
      const body = html`<div><${Controls} controls=${controls}/></div>`;
      if (title === p.title) return body;
      const radio = controls.find((k) => k.kind === "radio");
      return html`<${Group} key=${title} title=${title} tooltip=${radio ? radio.tooltip : ""}>${body}<//>`;
    });
    return html`<${Group} title=${p.title}><div class="groups">${groups}</div><//>`;
  }
```

A group named as the panel is drawn directly in the panel; any other becomes
a nested group box.

> **JavaScript:** A `Map` keeps insertion order (unlike `std::map`, which
> sorts), so groups appear in the order of their first control. `[...byGroup]`
> *spreads* the map into an array of `[key, value]` pairs so it can be
> `map`ped.

`Controls` ([app.js:265](../../applications/tagmonitor-rs/ui/app.js#L265))
stacks the radio groups and puts every other control in one label/field grid,
placed where the first non-radio control was:

```js
  function Controls({ controls }) {
    const rows = controls.filter((k) => k.kind !== "radio").map((k) => html`<${Fragment} key=${k.id}>
      <span class="kit-form-label">${k.label}</span><${Field} k=${k}/>
    <//>`);
    const formAt = controls.findIndex((k) => k.kind !== "radio");
    return controls.map((k, i) => (k.kind === "radio" ? html`<${Radios} key=${k.id} k=${k}/>`
      : i === formAt ? html`<div key="form" class="kit-form">${rows}</div>` : null));
  }
```

It returns an array of nodes, with `null` where nothing goes; a component may.
`Radios` ([app.js:279](../../applications/tagmonitor-rs/ui/app.js#L279)) draws
one `<input type="radio">` per choice with `checked=${k.value === c.id}` and
`onChange=${() => edit(k.id, c.id)}`, where `edit` is
`(id, value) => send("set_field", { id, value })` ([app.js:210](../../applications/tagmonitor-rs/ui/app.js#L210)).

### Number and date fields keep their focus

The interesting control is `Field`. A number field must show the engine's
value, but must not have it written back while the user is typing, or every
keystroke's state would overwrite the text. [app.js:294](../../applications/tagmonitor-rs/ui/app.js#L294):

```js
  function Field({ k }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
      const input = ref.current;
      if (k.kind === "check" || document.activeElement === input) return;
      if (k.kind === "number") {
        input.min = k.min; input.max = k.max; input.step = k.step;
        input.value = Number(k.value).toFixed(k.decimals);
      } else {
        input.min = utcText(Math.max(k.min, -62135596800));
        input.max = utcText(Math.min(k.max, 253402297200));
        input.value = utcText(k.value);
      }
    });
```

and its number branch, [app.js:313](../../applications/tagmonitor-rs/ui/app.js#L313):

```js
    if (k.kind === "number") {
      return html`<span class=${off}><input ref=${ref} class="kit-field number" type="number" title=${k.tooltip}
        disabled=${!k.enabled} onChange=${(ev) => {
          const v = parseFloat(ev.currentTarget.value);
          if (!Number.isNaN(v)) edit(k.id, v);
        }}/><span class="unit">${k.suffix}</span></span>`;
    }
```

The `<input>` is rendered **without** a `value` prop, so Preact never writes
its text. The layout effect, with no `deps`, runs after every render and
writes the engine's value, rounded to the control's `decimals`, *unless the
field has focus*. The DOM `change` event fires when a value is committed
(focus leaves, Return, or a click on the spinner arrows), not on every
keystroke (that is `input`), so the engine sees one `set_field` per committed
edit; it clamps or rounds it, and the field shows the engine's value once it
loses focus. In Qt terms: a `QDoubleSpinBox` that you update from the model
only when `!hasFocus()`, and whose `editingFinished` you forward. Decision
0038 notes this replaced Tag Monitor's earlier "rebuild-or-update logic".

The date branch uses `<input type="datetime-local" step="3600">` with two
helpers, [app.js:213](../../applications/tagmonitor-rs/ui/app.js#L213):

```js
  const utcText = (s) => new Date(s * 1000).toISOString().slice(0, 16);
  /** A datetime-local value, read as UTC, in epoch seconds; null if empty. */
  const utcSeconds = (text) => (text ? Math.round(Date.parse(text + ":00Z") / 1000) : null);
```

A `datetime-local` value has no time zone; appending `":00Z"` makes the
parse UTC, which is what the tag's schedule is in. The constants in `Field`
clamp the range to the years 0001 to 9999, outside which `toISOString`
throws.

> **JavaScript:** `parseFloat("abc")` is `NaN`, "not a number", which is not
> equal even to itself; hence `Number.isNaN(v)` rather than `v === NaN`.

### Prompts become dialogs, once each

The engine sends a new state whenever anything visible changes (often
several times a second while a tag is attached), and every one of them
carries the same `prompt` until it is answered. A reloaded page is also
sent the whole state again after its `ready`. So the page must show each
prompt once. [app.js:375](../../applications/tagmonitor-rs/ui/app.js#L375),
abridged:

```js
  async function showPrompt(p) {
    if (!p || p.id === shownPrompt) return;
    shownPrompt = p.id;
    const id = p.id;
    switch (p.kind) {
      case "alert": {
        const info = [p.informative, p.detail && p.detail !== p.informative ? p.detail : ""]
          .filter(Boolean).join("\n\n");
        await Kit.alert(p.title, p.text, info || null, p.style === "info" ? "info" : "warning");
        return answer(id, "ok");
      }
      case "chooseBase":
      case "chooseTagType": {
        const index = await Kit.chooseFromList(p.title, p.label, p.items, 0);
        return index === null ? answer(id, "cancel") : answer(id, "ok", { index });
      }
      case "startAnyway": {
        const body = html`<div><div>${p.text}</div><ul class="failures">${p.failures.map((f) => html`<li>${f}</li>`)}</ul></div>`;
        const choice = await Kit.dialog({ title: p.title, body, width: 420, buttons: [
          "spacer",
          { label: "Cancel", value: "cancel", cancel: true, default: true, enter: true },
          { label: "Start Anyway", value: "start" },
        ] });
        return answer(id, choice || "cancel");
      }
      // ...
      default:
        return answer(id, "cancel");
    }
  }
```

`shownPrompt` remembers the id on screen (or already answered); a state with
the same id is ignored, a new id opens a new dialog. The answer names the id,
and the engine ignores an answer for any other id, so a late answer cannot
settle the wrong question. An unknown kind is answered `cancel` at once, so
the engine never waits forever on a page that is older than it.

| `kind` | Drawn with | Answer |
| --- | --- | --- |
| `alert` | `Kit.alert` | `ok` |
| `chooseBase`, `chooseTagType` | `Kit.chooseFromList` | `ok {index}` or `cancel` |
| `startAnyway` | `Kit.dialog` with a Preact list of failed checks | `start` or `cancel` |
| `confirmErase` | `Kit.confirm`, OK destructive, Cancel the default | `ok` or `cancel` |
| `unsaved` | `Kit.confirm`: Discard, Cancel, Save | `discard`, `cancel` or `save` |
| `openFile` | `Kit.openFile` (native panel) | `ok {path}` or `cancel` |
| `saveFile` | `Kit.saveFile` (native panel) | `ok {path}` or `cancel` |

File pickers are prompts, not page features, so that the engine decides where
they start and what happens after: Save Configuration, Open Configuration and
Download Data all raise one. This is the page side of "dialogs as data"; the
engine side, with its queue of prompts, is [chapter 11](11-engine-and-model.md).

### Progress sheets follow the state

Erase and Download show a sheet whose progress comes from `state.erase` and
`state.download`. [app.js:338](../../applications/tagmonitor-rs/ui/app.js#L338):

```js
  function progressSheet(sheet, p, title, text, button, pressed) {
    if (!p) {
      if (sheet) sheet.close("closed");
      return null;
    }
    const body = html`<div class="progress-body">
      <div>${p.text || text}</div>
      <${Progress} fraction=${p.max > 0 ? Math.min(p.value, p.max) / p.max : null}/>
    </div>`;
    if (sheet) {
      sheet.update(body);
      return sheet;
    }
    sheet = Kit.dialog({ title, body,
      buttons: ["spacer", { label: button, value: "pressed", cancel: true, enter: true }] });
    sheet.then((v) => { if (v === "pressed") pressed(); });
    return sheet;
  }

  function renderProgress(s) {
    eraseSheet = progressSheet(eraseSheet, s.erase, "Erase", "Erasing flash...", "Close",
      () => { eraseSheet = null; send("hide_erase"); });
    downloadSheet = progressSheet(downloadSheet, s.download, "Download", "Downloading ..", "Cancel",
      () => { downloadSheet = null; send("cancel_download"); });
  }
```

The state's field non-null and no sheet: open one. Non-null and a sheet open:
`update` its body. Null: close it. The button sends `hide_erase` (the erase
continues on the tag; only the sheet goes) or `cancel_download`. This is the
whole `QProgressDialog` replacement: the dialog is a function of the state like
everything else.

### Menus come from the engine

The menu table is the answer to the `menus` query, each entry
`{kind: "command" | "item" | "separator", id, label, tooltip}`.
[app.js:438](../../applications/tagmonitor-rs/ui/app.js#L438), abridged:

```js
  function menuItem(e) {
    if (e.kind === "separator") return "-";
    const item = { label: e.label, tooltip: e.tooltip || undefined };
    if (e.kind === "command") {
      const a = state ? state.actions[e.id] : { enabled: false };
      item.disabled = !a.enabled;
      item.action = () => send(e.id);
      return item;
    }
    switch (e.id) {
      case "new_configuration":
        item.items = () => menus.newConfigurationTypes.map((t) => ({
          label: t, tooltip: "Start from this tag type's default configuration",
          action: () => send("new_config", { tagType: t }) }));
        break;
      // ...
      case "quit": item.action = () => send("quit"); break;
      case "user_guide": item.action = userGuide; break;
      case "about": item.action = about; break;
    }
    return item;
  }

  /** The menu bar's menus as Kit menus. */
  const kitMenus = () => menus.menus.map((m) => ({ title: m.title, items: () => m.entries.map(menuItem) }));
```

A `command` entry is uniform: enabled from `state.actions[id]`, and its action
is to send the id. An `item` entry is a page feature chosen by id: the New
Configuration and Open Recent submenus, the log items, Quit, the user guide
and About. Because `items` is a function, `menuItem` runs when the menu opens
and reads the state as it is then.

`popup(ev)` ([app.js:478](../../applications/tagmonitor-rs/ui/app.js#L478))
shows the same menus as submenus at the pointer, as qtmonitor's right-click
menu does; it is attached with `onContextMenu=${popup}` to the strip and two
pages. The keyboard shortcuts are a separate `keydown` listener
([app.js:515](../../applications/tagmonitor-rs/ui/app.js#L515)): Cmd or Ctrl
with O, S or Q, ignored while a dialog backdrop exists:

```js
  document.addEventListener("keydown", (ev) => {
    if (!(ev.metaKey || ev.ctrlKey) || ev.altKey || !state || document.querySelector(".kit-backdrop")) return;
    const key = ev.key.toLowerCase();
    const cmd = { o: "open", s: "save" }[key];
    if (cmd && state.actions[cmd].enabled) { ev.preventDefault(); send(cmd); }
    if (key === "q") { ev.preventDefault(); send("quit"); }
  });
```

> **JavaScript:** `{ o: "open", s: "save" }[key]` builds an object and
> indexes it in one expression: a lookup table instead of a `switch`. A
> missing key gives `undefined`, which is falsy.

The menu entries carry a `shortcut` field, but the page neither displays nor
uses it; the shortcuts are the three hard-coded here.

### The Error Log tab

The tab is a `LogPane`. Changing the level updates `ui.level`, redraws (so the
popup shows it) and tells the engine, which filters log records before it
emits them ([app.js:487](../../applications/tagmonitor-rs/ui/app.js#L487)):

```js
  function setLevel(level) {
    ui.level = level;
    draw();
    send("set_log_level", { level });
  }

  /** Save Error Log: asks for a file and has the engine write the log. */
  async function saveLog() {
    const path = await Kit.saveFile({ title: "Save Error Log", filters: [{ name: "Text", extensions: ["txt"] }] });
    if (path) send("save_text", { path, text: log.text() });
  }
```

The page has the text and the engine has the file system, so saving is "ask
for a path, then hand both to the engine".

## The mock engine and the preview parameters

Opened from disk in a browser, the same page runs against
[tagmonitor-rs/ui/mock-engine.js](../../applications/tagmonitor-rs/ui/mock-engine.js),
which defines one global, `MockEngine = { create }`. It holds hand-written
samples of the contract's shapes: the menu table (`MENUS`, line 29), a BitTag's
identity (`INFO`, line 69) and the engine's real BITTAG form (`FORM`, line 80,
regenerated with `cargo run -p tagmonitor-core --example form_view -- BITTAG`).
`create(params)` keeps a small simulated tag and document and computes a view
state from it. Its events are delivered as deep copies,
[mock-engine.js:95](../../applications/tagmonitor-rs/ui/mock-engine.js#L95):

```js
    const emit = (event, payload) => (listeners[event] || []).forEach((cb) => cb(clone(payload)));
```

where `clone` is `(o) => JSON.parse(JSON.stringify(o))` (line 24), so the page
never shares an object with the mock, exactly as with JSON from Rust. The
object it returns has the engine's three functions,
[mock-engine.js:230](../../applications/tagmonitor-rs/ui/mock-engine.js#L230):

```js
    return {
      command: (cmd, args) => Promise.resolve(command(cmd, args || {})),
      query: (name) => Promise.resolve(clone({
        menus: MENUS,
        about: { title: "About Tag Monitor", text: "Tag Monitor v2.1\n\nAttach to a tag through its USB base, check it, configure and start it, and download its data." },
        user_guide_url: "https://tag-designs.github.io/software/user/apps/qtmonitor.html",
      }[name] ?? null)),
      on: (event, cb) => { (listeners[event] = listeners[event] || []).push(cb); return Promise.resolve(); },
    };
```

> **JavaScript:** `x ?? y` (nullish coalescing) is `y` only when `x` is
> `null` or `undefined`, unlike `||`, which also replaces `0` and `""`. Here
> an unknown query answers `null`. (Optional chaining, `a?.b`, does not
> appear in these pages; they spell out `a && a.b`.)

The mock's `command` ([mock-engine.js:178](../../applications/tagmonitor-rs/ui/mock-engine.js#L178))
simulates enough to click through: attach and detach, Start (raising
`startAnyway` when not approved), Stop, Self-Test (a 1.2 s timer and a log
line), Erase, Download, Open and Save (raising their prompts), `set_field`
(kept as sent: "No rules here"), `answer`, and progress runs on a 250 ms
`setInterval`. The scenario is applied when the page sends `ready`
([mock-engine.js:180](../../applications/tagmonitor-rs/ui/mock-engine.js#L180)):

```js
        case "ready": scenario(params.get("scenario") || "idle"); return null;
```

Scenarios ([mock-engine.js:151](../../applications/tagmonitor-rs/ui/mock-engine.js#L151)):
`idle` (the default), `detached`, `running`, `finished`, `erasing`,
`downloading`, `prompt-start`, `prompt-unsaved`, `prompt-base`, `alert`. Unless
`?nopanel` is given, a `<select>` in the bottom-right corner switches them
live.

The URL parameters the three pages accept:

| Parameter | Effect | Read by |
| --- | --- | --- |
| `?scenario=<name>` | a canned state | each mock |
| `?nopanel` | hide the scenario switcher | each mock |
| `?theme=dark` or `light` | force a theme | `Kit.setupPage` |
| `?page=config`, `tag` or `log` | Tag Monitor's tab | `app.js:42` |
| `?page=calibrate`, `orientation` or `log` | Tag Calibrator's tab | its `app.js:37` |
| `?dialog=program&label=...`, `?dialog=conflict&label=...`, `?log` | Tag Programmer: open the Program dialog, the label-conflict question, the log drawer | its `app.js:37` and `451-452` |
| `?percent=N`, `?sample=N`, `?pose=h,p,r` | Tag Calibrator's replay position, or a fixed pose | its `mock-engine.js` |

The mock is a design tool, not a test oracle: it holds no rules and its
samples are hand-written, so when the contract changes, the mock must be
updated by hand.

## Pictures for the manual: headless Chrome

The user manual's screenshots of the three apps are taken from the pages and
their mocks, with no app and no tag. Each app has a `ui/screenshots.sh`;
Tag Monitor's, [screenshots.sh:12](../../applications/tagmonitor-rs/ui/screenshots.sh#L12):

```sh
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../../docs/src/images}
shot() { "$here/../../../ui-kit/screenshot.sh" "$here/index.html" "$out/$1.png" "$2" 1100 640; }

shot tagmonitor-configuration "scenario=idle&page=config"
shot tagmonitor-tag-info      "scenario=finished&page=tag"
shot tagmonitor-choose-base   "scenario=prompt-base&page=tag"
```

Each picture is a scenario and a tab at the window's opening size (1100 by
640, as `tauri.conf.json` gives it), written into `host/docs/src/images`. The
kit's script does the capture, [ui-kit/screenshot.sh:42](../../ui-kit/screenshot.sh#L42):

```sh
"$chrome" --headless --hide-scrollbars --force-device-scale-factor=1 \
  --virtual-time-budget=1500 --window-size="$width,$height" \
  --screenshot="$out" "file://$page?nopanel&theme=light${query:+&$query}" >/dev/null 2>&1
```

- `--headless --screenshot` makes Chrome load the page and write a PNG with no
  window.
- `--virtual-time-budget=1500` lets 1.5 s of page time pass before the
  capture, so the start-up promises, the mock's first state, a preview
  dialog and the mock's timers have run. Virtual time runs as fast as the
  page allows, not in real time.
- `?nopanel&theme=light` hides the scenario switcher and pins the light
  theme; `${query:+&$query}` appends the caller's parameters if any.

Because the pictures come from the mock, the kit README warns to look at them
before committing: the mock's samples can drift from what the engine sends.
The same mechanism was the regression test of the Preact port: decision 0038
records 30 Tag Programmer and 34 Tag Monitor screenshots identical before and
after. There is no JavaScript test framework in the tree.

## Try it

- Open `host/applications/tagmonitor-rs/ui/index.html` in a browser (double
  click it, or `open` it on macOS). Use the corner switcher to step through
  the scenarios; try `index.html?scenario=prompt-unsaved`,
  `?scenario=downloading&page=tag` and `?theme=dark`.
- In that page, type into an Active Threshold field and press Tab: the mock
  keeps the value as sent ("No rules here"); in the app the engine would clamp
  it. Then open the browser's developer tools console and call
  `Kit.alert("Test", "Hello", "Secondary text")` and
  `await Kit.chooseFromList("Pick", "One of:", ["a", "b"])`.
- Read the rest of `kit.js`'s dialog functions
  ([`chooseFromList`](../../ui-kit/kit.js#L456) measures text with a canvas
  to size the box) and `kit-components.js` end to end; together they are under
  800 lines.
- `rg -n "useState|useEffect|useLayoutEffect|useRef" host/ui-kit host/applications/*/ui/*.js`
  lists every hook in the pages: few enough to read each one.
- Take the manual's pictures into a scratch directory, leaving the committed
  ones alone: `host/applications/tagmonitor-rs/ui/screenshots.sh /tmp/shots`
  (needs Chrome or Chromium; set `CHROME` if it is not found).
- Compare the page's command list with the contract's
  [Commands](../../applications/tagmonitor-rs/design/ui-engine-contract.md#commands)
  table: `rg -n 'send\("' host/applications/tagmonitor-rs/ui/app.js`.

## Where next

- [Chapter 13](13-tracing-start.md) follows Tag Monitor's Start from the
  button in `Strip` through `invoke`, the engine thread and the tag thread to
  the wire, and back as a `state` event.
- [Chapter 14](14-the-three-applications.md) reads Tag Programmer's and Tag
  Calibrator's pages and engines against Tag Monitor's: the Program dialog
  with its own `useState`, and three.js in Preact effects.
- The kit's own manual, [ui-kit/README.md](../../ui-kit/README.md), lists every
  CSS class and how to update Preact.
