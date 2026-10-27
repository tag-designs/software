/**
 * @file
 * Tag Monitor's window: draws the engine's view state and turns user
 * actions into commands, per design/ui-engine-contract.md. It holds no
 * rules: the menus come from the engine's `menus` query, which commands are
 * enabled from the state's `actions`, every text from the engine.
 *
 * In a plain browser the engine is MockEngine (mock-engine.js). Preview
 * parameters: ?scenario=<name> picks a canned state (see mock-engine.js),
 * ?nopanel hides the scenario switcher, ?theme=dark|light forces a theme,
 * ?page=config|tag|log picks the tab (Configuration by default).
 *
 * Flow: the engine sends its whole view state (event `state`) after every
 * change and render() redraws from it. Every dialog the engine needs is a
 * prompt in the state (state.prompt); the page shows each once, by id, and
 * answers with `answer {id, button, index?, path?}`. The erase and download
 * progress sheets follow state.erase and state.download. What the page keeps
 * itself: the selected tab, the log text and the log level.
 */
(function () {
  "use strict";

  const { el } = Kit;
  const params = Kit.setupPage("Tag Monitor");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);
  /**
   * The element with `id` in index.html.
   * @param {string} id
   * @returns {?HTMLElement}
   */
  const $ = (id) => document.getElementById(id);

  let state = null;
  let menus = null;            // the `menus` query's answer
  let shownPrompt = null;      // the id of the prompt on screen or answered
  let eraseSheet = null;       // the open erase progress dialog
  let downloadSheet = null;    // the open download progress dialog
  let logLevel = "INFO";

  /**
   * Sends a command; an error string in the answer, or a rejected invoke,
   * is shown in an alert.
   * @param {string} cmd Command name from the contract.
   * @param {Object} [args] Its arguments.
   * @returns {Promise<void>}
   */
  const send = (cmd, args) =>
    Promise.resolve(engine.command(cmd, args)).then((err) => {
      if (err) Kit.alert("Tag Monitor", String(err), null, "warning");
    }, (err) => Kit.alert("Tag Monitor", String(err), null, "warning"));

  /**
   * Answers the prompt with `id`.
   * @param {number} id The prompt's id.
   * @param {string} button "ok", "cancel", or the prompt's own button name.
   * @param {Object} [extra] `index` or `path`.
   */
  const answer = (id, button, extra) => send("answer", Object.assign({ id, button }, extra || {}));

  // ---- Rendering --------------------------------------------------------

  /**
   * Redraws the window from a view state and shows its prompt if new.
   * @param {Object} s The engine's view state (see the contract).
   */
  function render(s) {
    state = s;
    Kit.setTitle(s.title);
    renderStrip(s.strip, s.actions);
    renderTag(s.tag);
    renderConfig(s.config);
    renderProgress(s);
    showPrompt(s.prompt);
  }

  /**
   * Sets a strip value and its warning colour.
   * @param {string} id Element id.
   * @param {{text: string, warn: boolean}} v
   */
  function strip(id, v) {
    $(id).textContent = v.text;
    $(id).classList.toggle("warn", !!v.warn);
  }

  /**
   * The status strip: link, type, state, clock, voltage, self-test,
   * approval and the Start and Stop buttons.
   * @param {Object} s The state's `strip`.
   * @param {Object} actions The state's `actions`.
   */
  function renderStrip(s, actions) {
    $("link").replaceChildren(
      el("span", { class: s.attached ? "dot-on" : "dot-off" }, "●"), " ",
      s.attached ? "Attached" : "Detached");
    $("s-type").textContent = s.type;
    $("s-type").title = s.typeTooltip;
    strip("s-state", s.state);
    strip("s-clock", s.clock);
    $("s-voltage").textContent = s.voltage;
    strip("s-test", s.test);
    $("approved").checked = s.approved;
    $("approved").disabled = !s.approvalEnabled;
    for (const [id, cmd] of [["b-start", "start"], ["b-stop", "stop"]]) {
      $(id).hidden = !actions[cmd].visible;
      $(id).disabled = !actions[cmd].enabled;
    }
  }

  /**
   * The Tag page: identity (kept, greyed, after a detach) and log counts.
   * @param {Object} t The state's `tag`.
   */
  function renderTag(t) {
    document.querySelector(".tag-groups").classList.toggle("disabled", !t.enabled);
    const info = t.info || {};
    for (const key of ["tagType", "board", "firmware", "flash", "flashExt", "uuid", "gitUrl", "sourcePath", "gitHash", "buildDate"]) {
      $("i-" + key).textContent = info[key] || "";
    }
    $("c-internal").textContent = t.internalCount;
    $("external").hidden = t.externalCount === null;
    $("c-external").textContent = t.externalCount || "";
  }

  /**
   * The Configuration page: the document's name and the engine's panels.
   * The form is rebuilt only when its panels or controls change; otherwise
   * each control's value, range and enabled state are updated in place, so
   * a field being typed into keeps its focus.
   * @param {Object} c The state's `config`.
   */
  function renderConfig(c) {
    $("config-none").hidden = c.document;
    $("config-doc").hidden = !c.document;
    $("config-name").textContent = c.name;
    $("config-modified").textContent = c.modified ? "unsaved changes" : "";
    const sig = c.panels.map((p) => p.name + ":" + p.controls.map((k) => k.id).join(",")).join("|");
    if (sig !== formSig) {
      formSig = sig;
      buildForm(c.panels);
    }
    for (const p of c.panels) for (const k of p.controls) updateControl(k);
  }

  let formSig = "";            // the panels and controls the form was built for
  let formNodes = {};          // control id -> its input elements

  /**
   * Sends one edit; the engine clamps or rounds it and the next state shows
   * the value it kept.
   * @param {string} id The control's id.
   * @param {*} value A number, a bool, a choice id, or epoch seconds.
   */
  const edit = (id, value) => send("set_field", { id, value });

  /** Epoch seconds as a datetime-local value in UTC ("2026-10-08T13:00"). */
  const utcText = (s) => new Date(s * 1000).toISOString().slice(0, 16);
  /** A datetime-local value, read as UTC, in epoch seconds; null if empty. */
  const utcSeconds = (text) => (text ? Math.round(Date.parse(text + ":00Z") / 1000) : null);

  /**
   * Builds the form: each panel a group, its controls in sub-groups by the
   * `group` the engine gives (a group named as the panel is the panel
   * itself), radio buttons stacked, everything else in label and field rows.
   * @param {Array<Object>} panels The state's `config.panels`.
   */
  function buildForm(panels) {
    formNodes = {};
    const host = $("config-panels");
    host.replaceChildren();
    for (const p of panels) {
      const groups = el("div", { class: "groups" });
      const byGroup = new Map();
      for (const k of p.controls) {
        if (!byGroup.has(k.group)) byGroup.set(k.group, []);
        byGroup.get(k.group).push(k);
      }
      for (const [title, controls] of byGroup) {
        const body = el("div", {});
        const radio = controls.find((k) => k.kind === "radio");
        let form = null;
        for (const k of controls) {
          if (k.kind === "radio") { body.append(radioNode(k)); continue; }
          if (!form) { form = el("div", { class: "kit-form" }); body.append(form); }
          form.append(el("span", { class: "kit-form-label" }, k.label), fieldNode(k));
        }
        if (title === p.title) { groups.append(body); continue; }
        groups.append(el("div", { class: "kit-group", title: radio ? radio.tooltip : "" },
          el("div", { class: "kit-group-title" }, title), el("div", { class: "kit-panel" }, body)));
      }
      host.append(el("div", { class: "kit-group" },
        el("div", { class: "kit-group-title" }, p.title), el("div", { class: "kit-panel" }, groups)));
    }
  }

  /**
   * Radio buttons for a choice control.
   * @param {Object} k The control.
   * @returns {HTMLElement}
   */
  function radioNode(k) {
    const inputs = [];
    const box = el("div", { class: "radios" }, k.choices.map((c) => {
      const input = el("input", { type: "radio", name: "f-" + k.id, value: String(c.id),
        onchange: () => edit(k.id, c.id) });
      inputs.push(input);
      return el("label", { title: c.tooltip || "" }, input, c.text);
    }));
    formNodes[k.id] = { k, inputs, box };
    return box;
  }

  /**
   * The field of a check box, number or date control.
   * @param {Object} k The control.
   * @returns {HTMLElement}
   */
  function fieldNode(k) {
    let input;
    let wrap;
    if (k.kind === "check") {
      input = el("input", { type: "checkbox", onchange: () => edit(k.id, input.checked) });
      wrap = el("label", { class: "check", title: k.tooltip }, input, k.text);
    } else if (k.kind === "number") {
      input = el("input", { class: "kit-field number", type: "number", title: k.tooltip,
        onchange: () => { const v = parseFloat(input.value); if (!Number.isNaN(v)) edit(k.id, v); } });
      wrap = el("span", {}, input, el("span", { class: "unit" }, k.suffix));
    } else {
      input = el("input", { class: "kit-field datetime", type: "datetime-local", step: "3600", title: k.tooltip,
        onchange: () => { const v = utcSeconds(input.value); if (v !== null) edit(k.id, v); } });
      wrap = el("span", {}, input, el("span", { class: "unit" }, "UTC"));
    }
    formNodes[k.id] = { k, inputs: [input], box: wrap };
    return wrap;
  }

  /**
   * Shows a control's value, range and enabled state; a field with focus is
   * left alone while the user types.
   * @param {Object} k The control from the state.
   */
  function updateControl(k) {
    const n = formNodes[k.id];
    if (!n) return;
    n.box.classList.toggle("disabled", !k.enabled);
    for (const input of n.inputs) input.disabled = !k.enabled;
    if (k.kind === "radio") {
      for (const input of n.inputs) input.checked = Number(input.value) === k.value;
      return;
    }
    const input = n.inputs[0];
    if (k.kind === "check") { input.checked = k.value; return; }
    if (document.activeElement === input) return;
    if (k.kind === "number") {
      input.min = k.min; input.max = k.max; input.step = k.step;
      input.value = Number(k.value).toFixed(k.decimals);
    } else {
      input.min = utcText(Math.max(k.min, -62135596800));
      input.max = utcText(Math.min(k.max, 253402297200));
      input.value = utcText(k.value);
    }
  }

  /**
   * Opens, updates or closes a progress sheet.
   * @param {?Object} sheet The open sheet, or null.
   * @param {?{value: number, max: number, text: ?string}} p Its progress, or
   *     null to close it.
   * @param {string} title The sheet's title.
   * @param {string} text Its text when `p` gives none.
   * @param {string} button The button's label.
   * @param {Function} pressed Called when the user presses the button.
   * @returns {?Object} The sheet now open, or null.
   */
  function progressSheet(sheet, p, title, text, button, pressed) {
    if (!p) {
      if (sheet) sheet.close("closed");
      return null;
    }
    if (!sheet) {
      const label = el("div", {});
      const bar = el("div", { class: "kit-progress" }, el("div", {}));
      sheet = Kit.dialog({ title, body: el("div", { class: "progress-body" }, label, bar),
        buttons: ["spacer", { label: button, value: "pressed", cancel: true, enter: true }] });
      sheet.label = label;
      sheet.bar = bar;
      sheet.then((v) => { if (v === "pressed") pressed(); });
    }
    sheet.label.textContent = p.text || text;
    sheet.bar.classList.toggle("busy", !(p.max > 0));
    sheet.bar.firstElementChild.style.width = p.max > 0 ? (100 * Math.min(p.value, p.max) / p.max) + "%" : "";
    return sheet;
  }

  /**
   * The erase and download progress sheets. Close only hides the erase,
   * which continues on the tag; Cancel stops the download after its page.
   * @param {Object} s The view state.
   */
  function renderProgress(s) {
    eraseSheet = progressSheet(eraseSheet, s.erase, "Erase", "Erasing flash...", "Close",
      () => { eraseSheet = null; send("hide_erase"); });
    downloadSheet = progressSheet(downloadSheet, s.download, "Download", "Downloading ..", "Cancel",
      () => { downloadSheet = null; send("cancel_download"); });
  }

  // ---- Prompts ----------------------------------------------------------

  /**
   * Shows the engine's prompt once per id, and answers it.
   * @param {?Object} p The state's `prompt`, or null.
   */
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
        const body = el("div", {}, el("div", {}, p.text),
          el("ul", { class: "failures" }, p.failures.map((f) => el("li", {}, f))));
        const choice = await Kit.dialog({ title: p.title, body, width: 420, buttons: [
          "spacer",
          { label: "Cancel", value: "cancel", cancel: true, default: true, enter: true },
          { label: "Start Anyway", value: "start" },
        ] });
        return answer(id, choice || "cancel");
      }
      case "confirmErase": {
        const ok = await Kit.confirm(p.title, p.text, null, [
          "spacer",
          { label: "Cancel", value: false, cancel: true, default: true, enter: true },
          { label: "OK", value: true, destructive: true },
        ]);
        return answer(id, ok ? "ok" : "cancel");
      }
      case "unsaved": {
        const choice = await Kit.confirm(p.title, p.text, p.informative, [
          { label: "Discard", value: "discard" },
          "spacer",
          { label: "Cancel", value: "cancel", cancel: true },
          { label: "Save", value: "save", default: true, enter: true },
        ], 460);
        return answer(id, choice || "cancel");
      }
      case "openFile": {
        const path = await Kit.openFile({ title: p.title, filters: p.filters, defaultPath: p.start });
        return path ? answer(id, "ok", { path }) : answer(id, "cancel");
      }
      case "saveFile": {
        const path = await Kit.saveFile({ title: p.title, filters: p.filters, defaultPath: p.suggested });
        return path ? answer(id, "ok", { path }) : answer(id, "cancel");
      }
      default:
        return answer(id, "cancel");
    }
  }

  // ---- Menus ------------------------------------------------------------

  const LEVELS = ["TRACE", "DEBUG", "INFO", "WARN", "ERROR"];

  /**
   * One menu entry from the `menus` query as a Kit menu item, with its
   * state read now.
   * @param {Object} e The entry: `{kind, id, label, tooltip}`.
   * @returns {Object|string} A Kit item, or "-".
   */
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
      case "open_recent":
        item.items = () => (state && state.recent.length)
          ? state.recent.map((r) => ({ label: r.name, tooltip: r.path, action: () => send("open_recent", { path: r.path }) }))
          : [{ label: "No recent files", disabled: true }];
        break;
      case "save_error_log": item.action = saveLog; break;
      case "clear_error_log": item.action = () => { $("log-text").value = ""; }; break;
      case "log_level":
        item.items = () => LEVELS.map((l) => ({ label: l, checked: l === logLevel, action: () => setLevel(l) }));
        break;
      case "quit": item.action = () => send("quit"); break;
      case "user_guide": item.action = userGuide; break;
      case "about": item.action = about; break;
    }
    return item;
  }

  /** The menu bar's menus as Kit menus. */
  const kitMenus = () => menus.menus.map((m) => ({ title: m.title, items: () => m.entries.map(menuItem) }));

  /**
   * The popup menu: the menu bar's menus as submenus, as qtmonitor's
   * right-click menu shows them.
   * @param {MouseEvent} ev The contextmenu event.
   */
  function popup(ev) {
    if (!menus) return;
    Kit.contextMenu(ev, menus.menus.map((m) => ({ label: m.title, items: () => m.entries.map(menuItem) })));
  }

  /**
   * Sets the log level, here and in the engine.
   * @param {string} level TRACE..ERROR.
   */
  function setLevel(level) {
    logLevel = level;
    $("log-level").value = level;
    send("set_log_level", { level });
  }

  /** Save Error Log: asks for a file and has the engine write the log. */
  async function saveLog() {
    const path = await Kit.saveFile({ title: "Save Error Log", filters: [{ name: "Text", extensions: ["txt"] }] });
    if (path) send("save_text", { path, text: $("log-text").value });
  }

  /** Opens the user guide at the URL the engine found. */
  async function userGuide() {
    const url = await engine.query("user_guide_url");
    if (url) Kit.openUrl(url);
    else Kit.alert("Tag Monitor User Guide", "The Tag Monitor User Guide was not found.", null, "info");
  }

  /** Shows the engine's About text. */
  async function about() {
    const a = await engine.query("about");
    Kit.alert(a.title, a.text, null, "info");
  }

  // ---- Wiring -----------------------------------------------------------

  /**
   * Shows one tab's page.
   * @param {string} page "tag", "config" or "log".
   */
  function showPage(page) {
    for (const b of document.querySelectorAll(".tabs button")) {
      b.setAttribute("aria-selected", String(b.dataset.page === page));
      $("page-" + b.dataset.page).hidden = b.dataset.page !== page;
    }
  }
  for (const b of document.querySelectorAll(".tabs button")) b.addEventListener("click", () => showPage(b.dataset.page));
  // The Configuration tab comes first: the status strip already shows what
  // matters most from the Tag tab.
  showPage(["tag", "config", "log"].includes(params.get("page")) ? params.get("page") : "config");

  for (const id of ["page-tag", "page-config", "strip"]) $(id).addEventListener("contextmenu", popup);
  $("approved").addEventListener("click", () => send("set_approved", { on: $("approved").checked }));
  $("b-start").addEventListener("click", () => send("start"));
  $("b-stop").addEventListener("click", () => send("stop"));
  $("save-log").addEventListener("click", saveLog);
  $("clear-log").addEventListener("click", () => { $("log-text").value = ""; });
  $("log-level").addEventListener("change", () => setLevel($("log-level").value));

  // The menus' standard shortcuts: Ctrl or Cmd with O, S and Q.
  document.addEventListener("keydown", (ev) => {
    if (!(ev.metaKey || ev.ctrlKey) || ev.altKey || !state || document.querySelector(".kit-backdrop")) return;
    const key = ev.key.toLowerCase();
    const cmd = { o: "open", s: "save" }[key];
    if (cmd && state.actions[cmd].enabled) { ev.preventDefault(); send(cmd); }
    if (key === "q") { ev.preventDefault(); send("quit"); }
  });

  const logText = $("log-text");
  Promise.all([
    engine.query("menus").then((m) => { menus = m; Kit.menuBar($("menubar"), kitMenus()); }),
    engine.on("log", (e) => {
      const atEnd = logText.scrollTop + logText.clientHeight >= logText.scrollHeight - 4;
      logText.value += `${e.level} ${e.text}\n`;
      if (atEnd) logText.scrollTop = logText.scrollHeight;
    }),
    engine.on("state", render),
  ]).then(() => send("set_log_level", { level: logLevel }))
    .then(() => send("ready"));
})();
