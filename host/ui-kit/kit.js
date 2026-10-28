/**
 * @file
 * Tag host UI kit: the engine connection, menus, dialogs, file pickers and
 * URL opening shared by the host applications' web front ends.
 *
 * A classic script, not an ES module, so a page works both inside the app
 * (Tauri) and opened straight from disk with file://, where browsers refuse
 * module imports. It defines one global, `Kit`. See README.md.
 *
 * The kit holds no application state or rules. Inside the app it relies on
 * `window.__TAURI__` (Tauri's `withGlobalTauri`) and, where present, the
 * dialog and opener plugins' globals; every feature has a browser fallback
 * so a page can be designed against a mock engine without the app. All
 * dialogs are drawn in the page (not native windows) and return promises.
 */
(function () {
  "use strict";

  /**
   * The Tauri global API, or null outside the app (or before Tauri has
   * injected it).
   * @returns {?object}
   */
  const tauri = () => (window.__TAURI__ && window.__TAURI__.core ? window.__TAURI__ : null);

  /**
   * True inside the Tauri app; false in a plain browser preview.
   * @returns {boolean}
   */
  function inApp() {
    return tauri() !== null;
  }

  /**
   * The engine: `command(cmd, args)`, `query(name, args)`, `on(event, cb)`.
   * Inside the app these go to the Tauri commands `command` and `query` and
   * the engine's events; in a browser they go to `mock`, which must provide
   * the same three functions.
   *
   * In the app, `command` resolves with null or rejects with the engine's
   * error string; `query` resolves with the answer's JSON; `on` resolves
   * once the listener is registered -- await it before the first command or
   * the first `state` event can be missed. Missing `args` are sent as `{}`.
   *
   * @param {?{command: Function, query: Function, on: Function}} mock
   *     Stand-in used only outside the app; ignored inside it.
   * @returns {{kind: string, command: Function, query: Function, on: Function}}
   *     `kind` is "tauri" or "mock".
   * @throws {Error} Outside the app when no mock is given.
   */
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

  // ---- DOM helpers ------------------------------------------------------

  /**
   * Creates an element: el("div", {class: "x", onclick: f}, child, "text").
   *
   * Attributes: `on*` functions become event listeners; `class` sets the
   * class name; `text` sets the text content; an object `style` is merged
   * into the style; `true` sets an empty (boolean) attribute; `undefined`,
   * `null` and `false` are skipped. Children may be nested arrays; nodes are
   * appended as they are, anything else as text (never parsed as HTML).
   *
   * @param {string} tag Element name.
   * @param {?Object<string, *>} attrs Attributes and listeners.
   * @param {...*} children Nodes, strings or arrays of them.
   * @returns {HTMLElement}
   */
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

  /**
   * Marks the page as a browser preview: shows the stand-in title bar and
   * applies ?theme=dark|light. Call once at start-up.
   *
   * The preview class (`kit-preview` on body) is added only outside the
   * app; the theme override applies in both.
   *
   * @param {string} [title] Window title to set, if any.
   * @returns {URLSearchParams} The page's query parameters, for the mock.
   */
  function setupPage(title) {
    const params = new URLSearchParams(location.search);
    const theme = params.get("theme");
    if (theme === "dark" || theme === "light") document.documentElement.dataset.theme = theme;
    if (!inApp()) document.body.classList.add("kit-preview");
    if (title) setTitle(title);
    return params;
  }

  /**
   * Sets the document title and the preview title bar's text.
   * @param {string} title
   */
  function setTitle(title) {
    if (document.title === title) return;
    document.title = title;
    for (const t of document.querySelectorAll(".kit-titlebar .kit-title")) t.textContent = title;
    // Tauri does not follow document.title. Setting the window's needs the
    // core:window:allow-set-title permission; without it the call is refused
    // and the window keeps the title tauri.conf.json gave it.
    const t = tauri();
    if (t && t.window && t.window.getCurrentWindow) {
      Promise.resolve(t.window.getCurrentWindow().setTitle(title)).catch(() => {});
    }
  }

  // ---- Menus ------------------------------------------------------------

  /** Closes the newest menu bar's open menu unless `target` is inside it;
   *  null until a menu bar is built. */
  let closeMenus = null;
  /** The document-level menu listeners have been installed. */
  let menuListening = false;

  /**
   * Builds an in-window menu bar into `host` from
   * [{title, items: [{label, action, checked, disabled} | "-"]}].
   * `items` may be a function returning the list, evaluated when the menu
   * opens, so check marks and enabled states follow the current state. An
   * item with its own `items` (a list or a function) is a submenu, which
   * opens to the right on hover.
   *
   * Menus open on mouse-down and track the pointer across titles while one
   * is open; a click outside or Escape closes it. Each call replaces the
   * contents of `host`. The document-level listeners that close a menu are
   * installed once, and act on the newest menu bar, so calling it again (to
   * rebuild the menus) is safe. Item `tooltip` sets the item's title.
   *
   * @param {HTMLElement} host Element to fill; gets class `kit-menubar`.
   * @param {Array<{title: string, items: (Array|Function)}>} menus
   */
  function menuBar(host, menus) {
    if (!menuListening) {
      menuListening = true;
      document.addEventListener("mousedown", (ev) => { if (closeMenus) closeMenus(ev.target); });
      document.addEventListener("keydown", (ev) => { if (ev.key === "Escape" && closeMenus) closeMenus(null); });
    }
    host.classList.add("kit-menubar");
    host.replaceChildren();
    let openMenu = null;
    const close = () => {
      if (openMenu) openMenu.classList.remove("open");
      openMenu = null;
    };
    for (const menu of menus) {
      const drop = el("div", { class: "kit-menu-dropdown" });
      const node = el("div", { class: "kit-menu" },
        el("button", {
          class: "kit-menu-title",
          type: "button",
          onmousedown: (ev) => {
            ev.preventDefault();
            if (openMenu === node) { close(); return; }
            close();
            fill(drop, typeof menu.items === "function" ? menu.items() : menu.items, close);
            node.classList.add("open");
            openMenu = node;
          },
          onmouseenter: () => {
            if (openMenu && openMenu !== node) {
              close();
              fill(drop, typeof menu.items === "function" ? menu.items() : menu.items, close);
              node.classList.add("open");
              openMenu = node;
            }
          },
        }, menu.title),
        drop);
      host.append(node);
    }
    // A press inside the open menu (target set and contained) keeps it open.
    closeMenus = (target) => {
      if (openMenu && !(target && openMenu.contains(target))) close();
    };
  }

  /**
   * Fills a menu's drop-down with `items`; an item fires its `action` on
   * mouse-up (unless disabled) after the menu closes.
   * @param {HTMLElement} drop The drop-down element, emptied first.
   * @param {Array<Object|string>} items Items, "-" for a separator.
   * @param {Function} close Closes the open menu.
   */
  function fill(drop, items, close) {
    drop.replaceChildren();
    for (const item of items) {
      if (item === "-") { drop.append(el("div", { class: "kit-menu-sep" })); continue; }
      const cls = ["kit-menu-item"];
      if (item.checked) cls.push("checked");
      if (item.disabled) cls.push("disabled");
      if (item.items) {
        // A submenu: filled now, shown by CSS while the item is hovered.
        const sub = el("div", { class: "kit-menu-dropdown kit-submenu" });
        fill(sub, typeof item.items === "function" ? item.items() : item.items, close);
        cls.push("kit-has-submenu");
        drop.append(el("div", { class: cls.join(" "), title: item.tooltip },
          el("span", { class: "kit-check" }, "✓"), item.label,
          el("span", { class: "kit-submenu-arrow" }, "▸"), sub));
        continue;
      }
      drop.append(el("div", {
        class: cls.join(" "),
        title: item.tooltip,
        onmouseup: () => {
          if (item.disabled) return;
          close();
          if (item.action) item.action();
        },
      }, el("span", { class: "kit-check" }, "✓"), item.label));
    }
  }

  /** Closes the open popup menu; null while none is open. */
  let closePopup = null;

  /**
   * Shows a popup (context) menu at the pointer, with `items` as for a menu
   * bar's menu, submenus included. A press outside it, Escape, or choosing
   * an item closes it. Call it from a `contextmenu` listener; it prevents
   * the browser's own menu.
   *
   * @param {MouseEvent} ev The contextmenu event, for its position.
   * @param {Array<Object|string>|Function} items The menu's items.
   */
  function contextMenu(ev, items) {
    ev.preventDefault();
    if (closePopup) closePopup();
    const drop = el("div", { class: "kit-menu-dropdown kit-popup" });
    const close = () => {
      drop.remove();
      document.removeEventListener("mousedown", outside, true);
      document.removeEventListener("keydown", escape, true);
      closePopup = null;
    };
    const outside = (e) => { if (!drop.contains(e.target)) close(); };
    const escape = (e) => { if (e.key === "Escape") close(); };
    fill(drop, typeof items === "function" ? items() : items, close);
    document.body.append(drop);
    // Keep it inside the window.
    const x = Math.min(ev.clientX, window.innerWidth - drop.offsetWidth - 4);
    const y = Math.min(ev.clientY, window.innerHeight - drop.offsetHeight - 4);
    drop.style.left = Math.max(0, x) + "px";
    drop.style.top = Math.max(0, y) + "px";
    document.addEventListener("mousedown", outside, true);
    document.addEventListener("keydown", escape, true);
    closePopup = close;
  }

  // ---- Dialogs ----------------------------------------------------------

  /**
   * Shows a modal dialog and resolves with the value of the button pressed.
   *
   * `buttons` are [{label, value, default, destructive, enabled}], left to
   * right; a button with `cancel: true` (or the value null) also answers
   * Escape. `body` is a Node, or a Preact element (from `Kit.html`), which
   * is rendered into the dialog and unmounted when it closes, so its
   * effects' clean-ups run. `title` is the dialog's window title.
   * `.update(body)` on the returned promise renders a new Preact body into
   * the open dialog (a progress sheet following the state); it does nothing
   * for a Node body.
   * Resolves with the chosen button's `value`. The returned promise has
   * `.dialog` (the element) and `.close(value)`.
   *
   * `default` only styles a button (and gives it focus when the body has no
   * input); `enter: true` is what makes Return press it, unless it is
   * disabled or focus is in a textarea. Escape with no cancel button
   * resolves null. The string "spacer" in `buttons` pushes the following
   * buttons right. Each button object gains a `.node` property (its
   * element), and the promise a `.buttons` property, so a caller can enable
   * or disable buttons while the dialog is open.
   *
   * @param {Object} options
   * @param {string} [options.title] Title line.
   * @param {Node|Object} options.body Content: a Node or a Preact element.
   * @param {Array<Object|string>} options.buttons Buttons, left to right.
   * @param {number} [options.width] Width in CSS pixels.
   * @param {Function} [options.onOpen] Called with the dialog element once
   *     it is in the document.
   * @returns {Promise<*>} With `.dialog`, `.close(value)`, `.update(body)`
   *     and `.buttons`.
   */
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
    box.append(content);
    const bar = el("div", { class: "kit-dialog-buttons" });
    for (const b of buttons) {
      if (b === "spacer") { bar.append(el("div", { class: "kit-spacer" })); continue; }
      const cls = ["kit-button"];
      if (b.default) cls.push("default");
      if (b.destructive) cls.push("destructive");
      const node = el("button", { class: cls.join(" "), type: "button", disabled: b.enabled === false,
        onclick: () => done(b.value) }, b.label);
      b.node = node;
      buttonNodes.push(node);
      bar.append(node);
    }
    box.append(bar);
    function done(value) {
      document.removeEventListener("keydown", onKey, true);
      backdrop.remove();
      if (vnode) window.preact.render(null, content);
      finish(value);
    }
    function onKey(ev) {
      if (ev.key === "Escape") {
        const cancel = buttons.find((b) => b !== "spacer" && (b.cancel || b.value === null));
        ev.preventDefault();
        done(cancel ? cancel.value : null);
      } else if (ev.key === "Enter" && !(ev.target instanceof HTMLTextAreaElement)) {
        const def = buttons.find((b) => b !== "spacer" && b.enter);
        if (def && !def.node.disabled) { ev.preventDefault(); done(def.value); }
      }
    }
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

  /**
   * The body of a message box: `text`, then the smaller `info` if given.
   * @param {string} text
   * @param {?string} info
   * @returns {HTMLElement}
   */
  function messageBody(text, info) {
    const body = el("div", {}, el("div", { class: "kit-message-text" }, text));
    if (info) body.append(el("div", { class: "kit-message-info" }, info));
    return body;
  }

  /**
   * A message box with one OK button. `style` ("info", "warning") is
   * accepted for the engine's alerts but not drawn differently.
   *
   * Return and Escape both press OK.
   *
   * @param {string} title
   * @param {string} text Main message.
   * @param {?string} [info] Secondary text.
   * @param {string} [style] Ignored.
   * @returns {Promise<boolean>} Resolves true when dismissed.
   */
  function alert(title, text, info, style) {
    return dialog({
      title,
      body: messageBody(text, info),
      buttons: [{ label: "OK", value: true, default: true, enter: true, cancel: true }],
      width: 420,
    });
  }

  /**
   * A question box. `buttons` as for dialog(); resolves with the chosen
   * value. Example: confirm("Erase Tag", "Erase the tag's log?", null,
   *   [{label: "Cancel", value: false, cancel: true, default: true},
   *    {label: "Erase", value: true, destructive: true}]).
   * `width` (px, default 440) widens the box for a long button row.
   *
   * @param {string} title
   * @param {string} text Main message.
   * @param {?string} info Secondary text, or null.
   * @param {Array<Object|string>} buttons As for dialog().
   * @param {number} [width] Width in CSS pixels.
   * @returns {Promise<*>} The chosen button's value.
   */
  function confirm(title, text, info, buttons, width) {
    return dialog({ title, body: messageBody(text, info), buttons, width: width || 440 });
  }

  /**
   * A one-line text prompt; resolves with the text, or null if cancelled.
   *
   * The initial text is selected when the box opens; Return presses OK.
   *
   * @param {string} title
   * @param {string} label Text above the field.
   * @param {string} [initial] Initial text.
   * @param {string} [placeholder] Placeholder shown when empty.
   * @returns {Promise<?string>}
   */
  function promptText(title, label, initial, placeholder) {
    const input = el("input", { class: "kit-field", type: "text", value: initial || "",
      placeholder: placeholder || "", style: { width: "100%" } });
    const body = el("div", {}, el("div", { style: { marginBottom: "8px" } }, label), input);
    const p = dialog({
      title, body, width: 400,
      buttons: [
        { label: "Cancel", value: null, cancel: true },
        { label: "OK", value: "ok", default: true, enter: true },
      ],
      onOpen: () => setTimeout(() => input.select(), 0),
    });
    return p.then((v) => (v === "ok" ? input.value : null));
  }

  /**
   * Chooses one of `items` (strings); resolves with the index or null.
   *
   * A click selects a row and a double-click chooses it at once. `current`
   * preselects a row; with none (or an invalid one) the first row is
   * selected, so OK always yields an index. The box widens, up to 760 px,
   * for long rows such as a base's description, so they need not wrap.
   *
   * @param {string} title
   * @param {string} label Text above the list.
   * @param {string[]} items Rows.
   * @param {number} [current] Index preselected.
   * @returns {Promise<?number>}
   */
  function chooseFromList(title, label, items, current) {
    let selected = Number.isInteger(current) && current >= 0 ? current : 0;
    const list = el("div", { class: "kit-list" });
    const rows = items.map((text, i) => el("div", {
      class: "kit-list-item" + (i === selected ? " selected" : ""),
      onclick: () => { selected = i; rows.forEach((r, j) => r.classList.toggle("selected", j === i)); },
      ondblclick: () => p.close(i),
    }, text));
    list.append(...rows);
    const body = el("div", {}, el("div", {}, label), list);
    // The widest row in the page's font, plus the dialog's and the row's
    // padding (about 90 px).
    const ctx = document.createElement("canvas").getContext("2d");
    ctx.font = getComputedStyle(document.body).font;
    const widest = Math.max(0, ...items.map((t) => ctx.measureText(String(t)).width));
    const p = dialog({
      title, body, width: Math.min(760, Math.max(460, Math.ceil(widest) + 90)),
      buttons: [
        { label: "Cancel", value: null, cancel: true },
        { label: "OK", value: "ok", default: true, enter: true },
      ],
    });
    return p.then((v) => (v === "ok" ? selected : v));
  }

  // ---- Files and URLs ---------------------------------------------------

  /**
   * Tauri's dialog plugin API, or null outside the app.
   * @returns {?object}
   */
  function dialogPlugin() {
    const t = tauri();
    return t && t.dialog ? t.dialog : null;
  }

  /**
   * Asks for an existing file. `filters`: [{name, extensions: ["elf"]}].
   * Resolves with a path or null. Without the Tauri dialog plugin (a browser
   * preview) it asks for the path as text.
   *
   * @param {Object} [options]
   * @param {string} [options.title] Dialog title.
   * @param {Array<{name: string, extensions: string[]}>} [options.filters]
   * @param {string} [options.defaultPath] Starting file or directory.
   * @returns {Promise<?string>} Absolute path, or null if cancelled.
   */
  async function openFile({ title, filters, defaultPath } = {}) {
    const d = dialogPlugin();
    if (d) {
      const path = await d.open({ title, filters, defaultPath, multiple: false, directory: false });
      return Array.isArray(path) ? path[0] || null : path || null;
    }
    return promptText(title || "Open", "Path of the file:", defaultPath || "", "/path/to/file");
  }

  /**
   * Asks for a file to write; resolves with a path or null.
   *
   * The native save panel confirms overwriting; nothing is written here.
   * Without the dialog plugin it asks for the path as text.
   *
   * @param {Object} [options] As for openFile().
   * @returns {Promise<?string>}
   */
  async function saveFile({ title, filters, defaultPath } = {}) {
    const d = dialogPlugin();
    if (d) return (await d.save({ title, filters, defaultPath })) || null;
    return promptText(title || "Save", "Path of the file to write:", defaultPath || "", "/path/to/file");
  }

  /**
   * Opens a URL in the user's browser (the opener plugin in the app, a new
   * tab in a preview). A `file://` URL opens the local manual.
   * @param {string} url
   * @returns {Promise<void>}
   */
  async function openUrl(url) {
    const t = tauri();
    if (t && t.opener) return t.opener.openUrl(url);
    window.open(url, "_blank", "noopener");
  }

  /**
   * Shows a file in the system's file manager (its folder, selected).
   *
   * Falls back to opening the containing folder when the opener plugin has
   * no reveal call, and in a browser preview to a message giving the path.
   *
   * @param {string} path Absolute path of the file.
   * @param {string} [title] Title of the browser-preview message; the app's
   *     name for the action, e.g. "Show CSV".
   * @returns {Promise<*>}
   */
  async function revealPath(path, title = "Show File") {
    const t = tauri();
    if (t && t.opener) {
      if (t.opener.revealItemInDir) return t.opener.revealItemInDir(path);
      return t.opener.openPath(path.replace(/[\\/][^\\/]*$/, ""));
    }
    return alert(title, "In the app this shows the file in its folder:", path, "info");
  }

  /** The kit's public API, the one global this script defines. */
  window.Kit = {
    inApp, createEngine, el, setupPage, setTitle, menuBar, contextMenu,
    dialog, alert, confirm, promptText, chooseFromList,
    openFile, saveFile, openUrl, revealPath,
  };
})();
