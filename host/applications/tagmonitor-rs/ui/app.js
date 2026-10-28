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
 * change, and draw() renders the window from it with Preact and the kit's
 * components (host/ui-kit/kit-components.js) into #app. Every dialog the
 * engine needs is a prompt in the state (state.prompt); the page shows each
 * once, by id, and answers with `answer {id, button, index?, path?}`. The
 * erase and download progress sheets follow state.erase and
 * state.download. What the page keeps itself: the selected tab, the log
 * text and the log level.
 */
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
  /**
   * What the page keeps itself: the tab shown (the Configuration tab comes
   * first, since the status strip already shows what matters most from the
   * Tag Info tab) and the log level.
   */
  const ui = {
    page: ["tag", "config", "log"].includes(params.get("page")) ? params.get("page") : "config",
    level: "INFO",
  };
  const log = Kit.logModel();

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

  /** Renders the window from the last state; nothing before the first. */
  function draw() {
    if (state) render(html`<${Window} s=${state}/>`, document.getElementById("app"));
  }

  /** The tabs: page, label, tooltip. */
  const TABS = [
    ["config", "Configuration", "The configuration to start the tag with"],
    ["tag", "Tag Info", "Tag identity and log counts. Right-click for tag commands."],
    ["log", "Error Log", "Messages from Tag Monitor and the tag"],
  ];

  /**
   * The whole window below the menu bar: tabs, status strip and the three
   * pages, all kept mounted so a hidden page keeps its scroll and the log
   * its text.
   * @param {{s: Object}} props The engine's view state (see the contract).
   * @returns {Object} A Preact element.
   */
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

  /**
   * A strip value with its warning colour.
   * @param {string} width The `w-*` class that reserves its width.
   * @param {{text: string, warn: boolean}} v The value.
   * @param {string} [tooltip]
   * @returns {Object} A Preact element.
   */
  const value = (width, v, tooltip) =>
    html`<span class=${`value ${width} kit-selectable` + (v.warn ? " warn" : "")} title=${tooltip}>${v.text}</span>`;

  /**
   * The status strip: link, type, state, clock, voltage, self-test,
   * approval and the Start and Stop buttons.
   * @param {Object} props
   * @param {Object} props.s The state's `strip`.
   * @param {Object} props.actions The state's `actions`.
   * @returns {Object} A Preact element.
   */
  function Strip({ s, actions }) {
    const clockTip = "Tag clock minus this computer's clock, in seconds";
    return html`<div id="strip" class="strip" onContextMenu=${popup}>
      <span class="link" title="Whether Tag Monitor is attached to a tag"
        ><span class=${s.attached ? "dot-on" : "dot-off"}>●</span> ${s.attached ? "Attached" : "Detached"}</span>
      <span class="caption" title="Attached tag type and board UUID">Type</span>
      ${value("w-type", { text: s.type }, s.typeTooltip)}
      <span class="caption" title="Tag state">State</span>${value("w-state", s.state)}
      <span class="caption" title=${clockTip}>Clock</span>${value("w-clock", s.clock, clockTip)}
      <span class="caption" title="Tag supply voltage">Voltage</span>
      ${value("w-voltage", { text: s.voltage }, "Tag supply voltage")}
      <span class="caption" title="Self-test result the tag reports">Self-test</span>
      ${value("w-test", s.test, "Self-test result the tag reports")}
      <span class="kit-spacer"></span>
      <label class="approved" title="Tick when you have reviewed the configuration. Start warns if it is not ticked; any change to the configuration clears it.">
        <input id="approved" type="checkbox" checked=${s.approved} disabled=${!s.approvalEnabled}
          onClick=${(ev) => send("set_approved", { on: ev.currentTarget.checked })}/> Approved
      </label>
      ${["start", "stop"].map((cmd) => html`<${Button} small hidden=${!actions[cmd].visible}
        disabled=${!actions[cmd].enabled} onClick=${() => send(cmd)}>${cmd === "start" ? "Start" : "Stop"}<//>`)}
    </div>`;
  }

  /** The Tag Information rows: info key, label, tooltip. */
  const INFO_ROWS = [
    ["tagType", "Tag Type", "Type of attached tag"],
    ["board", "Board Name"],
    ["firmware", "Firmware", "Description from Tag: Generally the tag type"],
    ["flash", "Flash Size", "Size of tag processor memory"],
    ["flashExt", "Ext. Flash Size", "Size of tag processor memory"],
    ["uuid", "UUID", "A unique code embedded in hardware that can be used to identify individual tags."],
    ["gitUrl", "Git Repo", "The location of the the tag software repository"],
    ["sourcePath", "Source Path"],
    ["gitHash", "Git Hash", "A code that can be used to identify the version of firmware currently running on the tag. " +
      "Corresponds to the first seven characters of the Git commit hash."],
    ["buildDate", "Build Date", "Tag firmware build date and time"],
  ];

  /**
   * A count box on the Tag Info page: a group whose panel shows "Count"
   * and the number.
   * @param {Object} props
   * @param {string} props.title The group's title.
   * @param {string} props.tooltip The panel's tooltip.
   * @param {string} [props.fieldTooltip] The number's tooltip.
   * @param {string} props.count The number, as text.
   * @returns {Object} A Preact element.
   */
  const Count = ({ title, tooltip, fieldTooltip, count, ...rest }) => html`<div class="kit-group" ...${rest}>
    <div class="kit-group-title">${title}</div>
    <div class="kit-panel count" title=${tooltip}>Count<span class="field kit-selectable" title=${fieldTooltip}>${count}</span></div>
  </div>`;

  /**
   * The Tag Info page: identity (kept, greyed, after a detach) and log
   * counts.
   * @param {{t: Object}} props The state's `tag`.
   * @returns {Object} A Preact element.
   */
  function TagPage({ t }) {
    const info = t.info || {};
    return html`<div class=${"tag-groups" + (t.enabled ? "" : " disabled")}>
      <${Group} class="info" title="Tag Information" titleTooltip="Information collected from attached tag">
        <div class="kit-form info-form">
          ${INFO_ROWS.map(([key, label, tooltip]) => html`<span class="kit-form-label">${label}</span><span
            class="field kit-selectable" title=${tooltip}>${info[key] || ""}</span>`)}
        </div>
      <//>
      <${Group} class="data" title="Data">
        <${Count} title="Logs" tooltip="Number of log entries (long words)"
          fieldTooltip="Number of entries in tag data log" count=${t.internalCount}/>
        <${Count} title="External Data" tooltip="Number of valid pages" count=${t.externalCount || ""}
          hidden=${t.externalCount === null}/>
      <//>
    </div>`;
  }

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
   * The Configuration page: the document's name and the engine's panels,
   * or a note when there is no document.
   * @param {{c: Object}} props The state's `config`.
   * @returns {Object} A Preact element.
   */
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

  /**
   * One panel: a group, its controls in sub-groups by the `group` the
   * engine gives (a group named as the panel is the panel itself).
   * @param {{p: Object}} props The panel from `config.panels`.
   * @returns {Object} A Preact element.
   */
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

  /**
   * A group's controls: radio buttons stacked, everything else in one label
   * and field form, placed where its first control comes.
   * @param {{controls: Array<Object>}} props The group's controls, in order.
   * @returns {Object} A Preact element.
   */
  function Controls({ controls }) {
    const rows = controls.filter((k) => k.kind !== "radio").map((k) => html`<${Fragment} key=${k.id}>
      <span class="kit-form-label">${k.label}</span><${Field} k=${k}/>
    <//>`);
    const formAt = controls.findIndex((k) => k.kind !== "radio");
    return controls.map((k, i) => (k.kind === "radio" ? html`<${Radios} key=${k.id} k=${k}/>`
      : i === formAt ? html`<div key="form" class="kit-form">${rows}</div>` : null));
  }

  /**
   * Radio buttons for a choice control.
   * @param {{k: Object}} props The control.
   * @returns {Object} A Preact element.
   */
  const Radios = ({ k }) => html`<div class=${"radios" + (k.enabled ? "" : " disabled")}>
    ${k.choices.map((c) => html`<label title=${c.tooltip || ""}><input type="radio" name=${"f-" + k.id}
      value=${String(c.id)} checked=${k.value === c.id} disabled=${!k.enabled}
      onChange=${() => edit(k.id, c.id)}/>${c.text}</label>`)}
  </div>`;

  /**
   * The field of a check box, number or date control.
   *
   * A number or date field is written from the state only while it does
   * not have focus, so the engine's answer to one keystroke never replaces
   * what the user is typing.
   * @param {{k: Object}} props The control.
   * @returns {Object} A Preact element.
   */
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
    const off = k.enabled ? undefined : "disabled";
    if (k.kind === "check") {
      return html`<label class=${"check" + (k.enabled ? "" : " disabled")} title=${k.tooltip}><input type="checkbox"
        checked=${k.value} disabled=${!k.enabled} onChange=${(ev) => edit(k.id, ev.currentTarget.checked)}/>${k.text}</label>`;
    }
    if (k.kind === "number") {
      return html`<span class=${off}><input ref=${ref} class="kit-field number" type="number" title=${k.tooltip}
        disabled=${!k.enabled} onChange=${(ev) => {
          const v = parseFloat(ev.currentTarget.value);
          if (!Number.isNaN(v)) edit(k.id, v);
        }}/><span class="unit">${k.suffix}</span></span>`;
    }
    return html`<span class=${off}><input ref=${ref} class="kit-field datetime" type="datetime-local" step="3600"
      title=${k.tooltip} disabled=${!k.enabled} onChange=${(ev) => {
        const v = utcSeconds(ev.currentTarget.value);
        if (v !== null) edit(k.id, v);
      }}/><span class="unit">UTC</span></span>`;
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
        const body = html`<div><div>${p.text}</div><ul class="failures">${p.failures.map((f) => html`<li>${f}</li>`)}</ul></div>`;
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
      case "clear_error_log": item.action = () => log.clear(); break;
      case "log_level":
        item.items = () => Kit.LEVELS.map((l) => ({ label: l, checked: l === ui.level, action: () => setLevel(l) }));
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
    ui.level = level;
    draw();
    send("set_log_level", { level });
  }

  /** Save Error Log: asks for a file and has the engine write the log. */
  async function saveLog() {
    const path = await Kit.saveFile({ title: "Save Error Log", filters: [{ name: "Text", extensions: ["txt"] }] });
    if (path) send("save_text", { path, text: log.text() });
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

  // The menus' standard shortcuts: Ctrl or Cmd with O, S and Q.
  document.addEventListener("keydown", (ev) => {
    if (!(ev.metaKey || ev.ctrlKey) || ev.altKey || !state || document.querySelector(".kit-backdrop")) return;
    const key = ev.key.toLowerCase();
    const cmd = { o: "open", s: "save" }[key];
    if (cmd && state.actions[cmd].enabled) { ev.preventDefault(); send(cmd); }
    if (key === "q") { ev.preventDefault(); send("quit"); }
  });

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
