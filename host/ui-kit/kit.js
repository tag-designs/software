/*
 * Tag host UI kit: the engine connection, menus, dialogs, file pickers and
 * URL opening shared by the host applications' web front ends.
 *
 * A classic script, not an ES module, so a page works both inside the app
 * (Tauri) and opened straight from disk with file://, where browsers refuse
 * module imports. It defines one global, `Kit`. See README.md.
 */
(function () {
  "use strict";

  const tauri = () => (window.__TAURI__ && window.__TAURI__.core ? window.__TAURI__ : null);

  /** True inside the Tauri app; false in a plain browser preview. */
  function inApp() {
    return tauri() !== null;
  }

  /**
   * The engine: `command(cmd, args)`, `query(name, args)`, `on(event, cb)`.
   * Inside the app these go to the Tauri commands `command` and `query` and
   * the engine's events; in a browser they go to `mock`, which must provide
   * the same three functions.
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

  /** Creates an element: el("div", {class: "x", onclick: f}, child, "text"). */
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

  /** Marks the page as a browser preview: shows the stand-in title bar and
   *  applies ?theme=dark|light. Call once at start-up. */
  function setupPage(title) {
    const params = new URLSearchParams(location.search);
    const theme = params.get("theme");
    if (theme === "dark" || theme === "light") document.documentElement.dataset.theme = theme;
    if (!inApp()) document.body.classList.add("kit-preview");
    if (title) setTitle(title);
    return params;
  }

  function setTitle(title) {
    document.title = title;
    for (const t of document.querySelectorAll(".kit-titlebar .kit-title")) t.textContent = title;
  }

  // ---- Menus ------------------------------------------------------------

  /**
   * Builds an in-window menu bar into `host` from
   * [{title, items: [{label, action, checked, disabled} | "-"]}].
   * `items` may be a function returning the list, evaluated when the menu
   * opens, so check marks and enabled states follow the current state.
   */
  function menuBar(host, menus) {
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
    document.addEventListener("mousedown", (ev) => {
      if (openMenu && !openMenu.contains(ev.target)) close();
    });
    document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") close(); });
  }

  function fill(drop, items, close) {
    drop.replaceChildren();
    for (const item of items) {
      if (item === "-") { drop.append(el("div", { class: "kit-menu-sep" })); continue; }
      const cls = ["kit-menu-item"];
      if (item.checked) cls.push("checked");
      if (item.disabled) cls.push("disabled");
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

  // ---- Dialogs ----------------------------------------------------------

  /**
   * Shows a modal dialog and resolves with the value of the button pressed.
   *
   * `buttons` are [{label, value, default, destructive, enabled}], left to
   * right; a button with `cancel: true` (or the value null) also answers
   * Escape. `body` is a Node. `title` is the dialog's window title.
   * Resolves with the chosen button's `value`. The returned promise has
   * `.dialog` (the element) and `.close(value)`.
   */
  function dialog({ title, body, buttons, width, onOpen }) {
    let finish;
    const promise = new Promise((resolve) => { finish = resolve; });
    const buttonNodes = [];
    const box = el("div", { class: "kit-dialog", role: "dialog", "aria-modal": "true",
      style: width ? { width: width + "px" } : undefined });
    const backdrop = el("div", { class: "kit-backdrop" }, box);
    if (title) box.append(el("div", { class: "kit-dialog-title" }, title));
    box.append(el("div", { class: "kit-dialog-body" }, body));
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
    promise.buttons = buttons;
    return promise;
  }

  function messageBody(text, info) {
    const body = el("div", {}, el("div", { class: "kit-message-text" }, text));
    if (info) body.append(el("div", { class: "kit-message-info" }, info));
    return body;
  }

  /** A message box with one OK button. `style` ("info", "warning") is
   *  accepted for the engine's alerts but not drawn differently. */
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
   */
  function confirm(title, text, info, buttons, width) {
    return dialog({ title, body: messageBody(text, info), buttons, width: width || 440 });
  }

  /** A one-line text prompt; resolves with the text, or null if cancelled. */
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

  /** Chooses one of `items` (strings); resolves with the index or null. */
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
    const p = dialog({
      title, body, width: 460,
      buttons: [
        { label: "Cancel", value: null, cancel: true },
        { label: "OK", value: "ok", default: true, enter: true },
      ],
    });
    return p.then((v) => (v === "ok" ? selected : v));
  }

  // ---- Files and URLs ---------------------------------------------------

  function dialogPlugin() {
    const t = tauri();
    return t && t.dialog ? t.dialog : null;
  }

  /**
   * Asks for an existing file. `filters`: [{name, extensions: ["elf"]}].
   * Resolves with a path or null. Without the Tauri dialog plugin (a browser
   * preview) it asks for the path as text.
   */
  async function openFile({ title, filters, defaultPath } = {}) {
    const d = dialogPlugin();
    if (d) {
      const path = await d.open({ title, filters, defaultPath, multiple: false, directory: false });
      return Array.isArray(path) ? path[0] || null : path || null;
    }
    return promptText(title || "Open", "Path of the file:", defaultPath || "", "/path/to/file");
  }

  /** Asks for a file to write; resolves with a path or null. */
  async function saveFile({ title, filters, defaultPath } = {}) {
    const d = dialogPlugin();
    if (d) return (await d.save({ title, filters, defaultPath })) || null;
    return promptText(title || "Save", "Path of the file to write:", defaultPath || "", "/path/to/file");
  }

  /** Opens a URL in the user's browser. */
  async function openUrl(url) {
    const t = tauri();
    if (t && t.opener) return t.opener.openUrl(url);
    window.open(url, "_blank", "noopener");
  }

  /** Shows a file in the system's file manager (its folder, selected). */
  async function revealPath(path) {
    const t = tauri();
    if (t && t.opener) {
      if (t.opener.revealItemInDir) return t.opener.revealItemInDir(path);
      return t.opener.openPath(path.replace(/[\\/][^\\/]*$/, ""));
    }
    return alert("Show CSV", "In the app this shows the file in its folder:", path, "info");
  }

  window.Kit = {
    inApp, createEngine, el, setupPage, setTitle, menuBar,
    dialog, alert, confirm, promptText, chooseFromList,
    openFile, saveFile, openUrl, revealPath,
  };
})();
