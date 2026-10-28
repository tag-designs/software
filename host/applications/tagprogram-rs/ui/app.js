/**
 * @file
 * Tag Programmer's window: draws the engine's view state and turns user
 * actions into commands and queries, per design/ui-engine-contract.md. It
 * holds no rules; every text it shows comes from the engine, except the
 * dialogs' fixed wording, which is qtprogram's.
 *
 * In a plain browser the engine is MockEngine (mock-engine.js). Preview
 * parameters: ?scenario=<name> picks a canned state (see mock-engine.js),
 * ?nopanel hides the scenario switcher, ?theme=dark|light forces a theme,
 * ?dialog=program[&label=...] opens the Program dialog, ?log opens the log.
 *
 * Flow: the engine sends its whole view state (event `state`) after every
 * change, and draw() renders the window from it with Preact and the kit's
 * components (host/ui-kit/kit-components.js) into #app. The page keeps only
 * what is its own: whether the log is open, the log level, the log text,
 * and which prompt has been shown. User actions become `command`s, which
 * the engine answers with a new state, and the dialogs that need data
 * first ask `query`s (program_preview, label_notes, label_conflict, ...).
 * Prompts the engine raises (state.prompt) are answered with a command.
 */
(function () {
  "use strict";

  const { html, render } = Kit;
  const { useEffect, useRef, useState } = Kit.hooks;
  const { Button, Disclosure, Select, Group, Row, Progress, StatusGrid, LogPane } = Kit.ui;
  const params = Kit.setupPage("Tag Programmer");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);

  let state = null;
  let shownPrompt = null;     // the id of the prompt on screen or answered
  let busy = false;           // a dialog of ours is open; ignore re-entry
  /** What the page keeps itself: the log drawer and the log level. */
  const ui = { logOpen: params.has("log"), level: "INFO" };
  const log = Kit.logModel();

  /**
   * Sends a command to the engine; an error string in the answer, or a
   * rejected invoke, is shown in an alert. Resolves once the engine has
   * accepted the command, not when it has finished.
   * @param {string} cmd Command name from the contract.
   * @param {Object} [args] Its arguments.
   * @returns {Promise<void>}
   */
  const send = (cmd, args) =>
    Promise.resolve(engine.command(cmd, args)).then((err) => {
      if (err) Kit.alert("Tag Programmer", String(err), null, "warning");
    }, (err) => Kit.alert("Tag Programmer", String(err), null, "warning"));

  // ---- Rendering --------------------------------------------------------

  /** Renders the window from the last state; nothing before the first. */
  function draw() {
    if (state) render(html`<${Window} s=${state}/>`, document.getElementById("app"));
  }

  /** A closed <select> collapses runs of spaces; qtprogram's texts use two. */
  const keepSpaces = (text) => text.replace(/  /g, "  ");

  /**
   * The firmware combo's entries from `fw.items`, with a "Choose firmware"
   * placeholder while nothing is selected.
   * @param {Object} fw The state's `firmware` section.
   * @returns {Array<Object>} Entries for Select.
   */
  function firmwareOptions(fw) {
    const options = fw.selected === "" ? [{ value: "", text: "Choose firmware" }] : [];
    for (const item of fw.items) {
      options.push(item.separator ? { value: "-", text: "──────────", disabled: true }
        : { value: item.id, text: keepSpaces(item.text) });
    }
    return options;
  }

  /**
   * The status rows, with Choose... on the Record row and Stop and Erase on
   * the Tag state row, as `rb` says.
   * @param {Array<Object>} checks The state's `checks` rows, in order.
   * @param {Object} rb The state's `rowButtons`.
   * @returns {Array<Object>} Rows for StatusGrid.
   */
  function checkRows(checks, rb) {
    return checks.map((c) => {
      let buttons = null;
      if (c.name === "Record" && rb.recordChoose) {
        buttons = html`<${Button} small onClick=${() => send("choose_record")}>Choose...<//>`;
      }
      if (c.name === "Tag state") {
        buttons = html`
          ${rb.stop.visible && html`<${Button} small disabled=${!rb.stop.enabled} onClick=${() => send("stop_tag")}>Stop<//>`}
          ${rb.erase.visible && html`<${Button} small disabled=${!rb.erase.enabled} onClick=${confirmErase}>Erase<//>`}`;
      }
      return { level: c.level, name: c.name, text: c.text, buttons };
    });
  }

  /**
   * The Tag panel: identity, state, firmware, tests, clock (flagged when
   * off) and the result, coloured when it is a pass or a failure.
   * @param {{t: Object}} props The state's `tag` section.
   * @returns {Object} A Preact element.
   */
  function TagPanel({ t }) {
    const result = t.result.ok === true ? "kit-text-ok" : t.result.ok === false ? "kit-text-fail" : "";
    return html`<div class="kit-form">
      <${Row} single label="Label" value=${t.label}/>
      <${Row} single label="UUID" value=${t.uuid}/>
      <${Row} single label="State" value=${t.state}/>
      <${Row} single label="Firmware" value=${t.firmware}/>
      <${Row} single label="Tests" value=${t.tests}/>
      <span class="kit-form-label">Clock</span>
      <span class="kit-form-value clock-row">
        <span id="tag-clock" class=${"kit-selectable" + (t.clock.warn ? " kit-text-warn" : "")}
          title="The tag's clock minus this computer's, in seconds">${t.clock.text}</span>
        <${Button} small disabled=${!t.syncEnabled} title="Set the tag's clock to this computer's"
          onClick=${() => send("sync_clock")}>Sync<//>
      </span>
      <${Row} single label="Result" class=${result} value=${t.result.text}/>
    </div>`;
  }

  /** The action buttons: state key, label, the Kit button kind, handler. */
  const ACTIONS = [
    ["program", "Program", "default", () => programDialog()],
    ["retest", "Retest", null, () => retestDialog()],
    ["nextTag", "Next Tag", null, () => send("next_tag")],
    ["attach", "Attach", null, () => send("attach")],
  ];

  /**
   * The step line, the progress bar (hidden for null, indeterminate for
   * "busy", else `done`/`total`) and the Retry and Cancel buttons.
   * @param {{s: Object}} props The state's `session` section.
   * @returns {Object} A Preact element.
   */
  function Session({ s }) {
    const p = s.progress;
    const fraction = p && typeof p === "object" ? (p.total > 0 ? p.done / p.total : 0) : null;
    return html`<div class="session">
      <span id="step" class="kit-selectable">${s.stepText}</span>
      <${Progress} hidden=${p === null} fraction=${fraction}/>
      <${Button} hidden=${!s.retryVisible} onClick=${() => send("retry_connect")}>Retry<//>
      <${Button} hidden=${!s.cancelVisible} onClick=${() => send("cancel_session")}>${s.cancelText}<//>
    </div>`;
  }

  /**
   * The whole window below the menu bar.
   * @param {{s: Object}} props The engine's view state (see the contract).
   * @returns {Object} A Preact element.
   */
  function Window({ s }) {
    const fw = s.firmware;
    const t = s.tagType;
    return html`
      <div class="choice">
        <label for="firmware">Firmware</label>
        <${Select} id="firmware" options=${firmwareOptions(fw)} value=${fw.selected} disabled=${!fw.enabled}
          onChange=${firmwareChosen}/>
        <${Button} id="release-notes" disabled=${!fw.releaseNotesUrl}
          onClick=${() => fw.releaseNotesUrl && Kit.openUrl(fw.releaseNotesUrl)}>Release Notes<//>
        <label for="target">Tag type</label>
        <${Select} id="target" options=${t.items} value=${t.selected} disabled=${!t.enabled}
          onChange=${(name) => send("choose_target", { name })}/>
        <span id="target-note">${t.note}</span>
      </div>

      <div class="panels">
        <${Group} class="station" title="Station"><${StatusGrid} id="checks" rows=${checkRows(s.checks, s.rowButtons)}/><//>
        <${Group} class="tag" title="Tag"><${TagPanel} t=${s.tag}/><//>
      </div>

      <div class="actions">
        ${ACTIONS.map(([key, label, kind, action]) => html`<${Button} kind=${kind} disabled=${!s.actions[key].enabled}
          title=${s.actions[key].enabled ? "" : s.actions[key].tooltip} onClick=${action}>${label}<//>`)}
        <span class="kit-spacer"></span>
        <${Button} onClick=${() => s.recordPath && Kit.revealPath(s.recordPath, "Show CSV")}>Show CSV<//>
      </div>

      <${Session} s=${s.session}/>

      <div class="bottom">
        <span id="batch">${s.batch}</span>
        <${Disclosure} pressed=${ui.logOpen} onClick=${() => { ui.logOpen = !ui.logOpen; draw(); }}>Log<//>
      </div>

      <${LogPane} hidden=${!ui.logOpen} log=${log} level=${ui.level} onSave=${saveLog}
        onLevel=${(level) => { ui.level = level; draw(); send("set_log_level", { level }); }}/>`;
  }

  /** Save Error Log: asks for a file and has the engine write the log. */
  async function saveLog() {
    const path = await Kit.saveFile({ title: "Save Log" });
    if (path) send("save_text", { path, text: log.text() });
  }

  // ---- Prompts ----------------------------------------------------------

  /**
   * Shows the engine's prompt once per prompt id; each kind answers with
   * its command, and an unknown kind is cancelled so the engine never waits.
   * @param {?Object} p The state's `prompt`, or null.
   */
  function showPrompt(p) {
    if (!p || p.id === shownPrompt) return;
    shownPrompt = p.id;
    if (p.kind === "autoAttach") return promptAutoAttach();
    if (p.kind === "recordFile") return promptRecordFile(p);
    if (p.kind === "chooseBase") return promptChooseBase(p);
    if (p.kind === "alert") {
      return Kit.alert(p.title, p.text, null, p.style).then(() => send("prompt_cancel"));
    }
    send("prompt_cancel");
  }

  /** The first-run question: attach automatically? Answers `answer_auto_attach`. */
  async function promptAutoAttach() {
    const on = await Kit.confirm("Attach Automatically?",
      "Tag Programmer can connect to each tag as soon as it finds the base, so its state and identity " +
      "are shown without pressing Attach.\n\nConnecting resets the tag, which ends a deployment that is " +
      "running. Attach automatically from now on? (Station > Attach Automatically changes this.)",
      null,
      [{ label: "No", value: false, default: true, enter: true, cancel: true }, { label: "Yes", value: true }]);
    send("answer_auto_attach", { on });
  }

  /**
   * Asks which record file to use: the default (open or create it), an
   * existing file, or a new one elsewhere. Cancelling, at either step, sends
   * `prompt_cancel`.
   * @param {Object} p The prompt, with `defaultPath` and `exists`.
   */
  async function promptRecordFile(p) {
    const filters = [{ name: "CSV", extensions: ["csv"] }];
    const choice = await Kit.confirm("Record File",
      "Tag Programmer records every tag it programs in a CSV file.",
      p.exists ? `Use the record at ${p.defaultPath}, or choose another?`
        : `Create one at ${p.defaultPath}, or choose another location?`,
      [
        { label: "Cancel", value: "cancel", cancel: true },
        "spacer",
        { label: "Open Existing...", value: "open" },
        { label: "Create Elsewhere...", value: "create" },
        { label: p.exists ? "Use It" : "Create", value: "default", default: true, enter: true },
      ], 560);
    if (choice === "default") return send("record_use_default");
    if (choice === "open") {
      const path = await Kit.openFile({ title: "Record file", filters, defaultPath: p.defaultPath });
      if (path) return send("record_open", { path });
    } else if (choice === "create") {
      const path = await Kit.saveFile({ title: "New record file", filters, defaultPath: p.defaultPath });
      if (path) return send("record_create", { path });
    }
    send("prompt_cancel");
  }

  /**
   * Asks which of several bases to use; answers `choose_base` with the
   * index, or `prompt_cancel`.
   * @param {Object} p The prompt, with `label`, `items` and `current`.
   */
  async function promptChooseBase(p) {
    const index = await Kit.chooseFromList("Choose a Base", p.label, p.items, p.current);
    send(index === null ? "prompt_cancel" : "choose_base", index === null ? {} : { index });
  }

  // ---- Dialogs started by the user --------------------------------------

  /** Confirms erasing the tag's log, Cancel the default, then sends `erase_tag`. */
  async function confirmErase() {
    const ok = await Kit.confirm("Erase Tag",
      "Erase the tag's log? Download it with Tag Monitor first; it cannot be recovered.", null,
      [{ label: "Cancel", value: false, default: true, enter: true, cancel: true },
       { label: "Erase", value: true, destructive: true }]);
    if (ok) send("erase_tag");
  }

  /**
   * Whether to go ahead with `label`: when the record already holds it for
   * another tag, asks, with Cancel the default, since two boards sharing a
   * label make the record ambiguous.
   * @param {string} title Title of the dialog asking.
   * @param {string} label The trimmed label.
   * @returns {Promise<boolean>} True to go ahead.
   */
  async function labelAccepted(title, label) {
    const conflict = await engine.query("label_conflict", { label });
    if (!conflict) return true;
    return Kit.confirm(title, conflict,
      "Two tags with one label cannot be told apart in the record. Check the label on the board.",
      [{ label: "Cancel", value: false, cancel: true, default: true, enter: true },
       { label: "Use This Label", value: true, destructive: true }], 460);
  }

  /**
   * The Program dialog's body: the label field, what programming will
   * erase, lose and keep, and the notes for the label as it is typed (a
   * stale answer is dropped if a newer one was asked for).
   * @param {Object} props
   * @param {Object} props.pv The program_preview answer.
   * @param {string} props.initialLabel Label to start with.
   * @param {Function} props.onLabel Called with the trimmed label after
   *     each change, and once at the start.
   * @returns {Object} A Preact element.
   */
  function ProgramBody({ pv, initialLabel, onLabel }) {
    const [label, setLabel] = useState(initialLabel);
    const [notes, setNotes] = useState("");
    const seq = useRef(0);
    const input = useRef(null);
    useEffect(() => { setTimeout(() => input.current.select(), 0); }, []);
    useEffect(() => {
      const trimmed = label.trim();
      onLabel(trimmed);
      const mine = ++seq.current;
      Promise.resolve(trimmed ? engine.query("label_notes", { label: trimmed }) : []).then((extra) => {
        if (mine === seq.current) setNotes(pv.notes.concat(extra || []).join("\n"));
      });
    }, [label]);
    const bullets = (items) => html`<ul>${items.map((t) => html`<li>${t}</li>`)}</ul>`;
    return html`<div>
      <div class="kit-form program-form">
        <span class="kit-form-label">Label</span>
        <div class="kit-form-value"><input ref=${input} class="kit-field" type="text" value=${label}
          placeholder="The label on the board, e.g. BitTag-017"
          onInput=${(ev) => setLabel(ev.currentTarget.value)}/></div>
        <${Row} label="Tag" value=${pv.tag}/>
        <${Row} label="Image" value=${pv.image}/>
        <${Row} label="Erase" value=${pv.erase}/>
        <${Row} label="Lost">${bullets(pv.lost)}<//>
        <${Row} label="Kept">${bullets(pv.kept)}<//>
      </div>
      <div class="program-notes">${notes || null}</div>
    </div>`;
  }

  /**
   * qtprogram's Program dialog, built from program_preview and label_notes.
   *
   * Erase and Program stays disabled while the label is empty; Return
   * presses Cancel, so the destructive step always takes a click. Re-entry
   * while open is ignored.
   * @param {string} [initialLabel] Label to start with.
   */
  async function programDialog(initialLabel) {
    if (busy) return;
    busy = true;
    try {
      const pv = await engine.query("program_preview");
      if (!pv.ok) {
        await Kit.alert("Program Tag", pv.reason, null, "warning");
        return;
      }
      const buttons = [
        { label: "Cancel", value: null, default: false, cancel: true, enter: true },
        { label: "Erase and Program", value: "program", default: true, enabled: false },
      ];
      let label = "";
      const onLabel = (l) => {
        label = l;
        if (buttons[1].node) buttons[1].node.disabled = l === "";
      };
      const body = html`<${ProgramBody} pv=${pv} initialLabel=${initialLabel || ""} onLabel=${onLabel}/>`;
      if ((await Kit.dialog({ title: "Program Tag", body, buttons })) !== "program") return;
      if (await labelAccepted("Program Tag", label)) send("program", { label });
    } finally {
      busy = false;
    }
  }

  /**
   * Asks for the label of the tag to retest (prefilled by the engine) and
   * sends `retest` once any label conflict is confirmed.
   */
  async function retestDialog() {
    const label = await Kit.promptText("Retest Tag", "Label of the tag to retest:",
      await engine.query("retest_default_label"));
    if (label === null || label.trim() === "") return;
    if (await labelAccepted("Retest Tag", label.trim())) send("retest", { label: label.trim() });
  }

  /**
   * Handles a firmware combo change: "Other ELF file..." opens a file
   * picker, anything else is sent as `choose_firmware`.
   * @param {string} id The chosen entry's value.
   */
  async function firmwareChosen(id) {
    if (id === "other") {
      const path = await Kit.openFile({ title: "Firmware image",
        filters: [{ name: "ELF images", extensions: ["elf"] }] });
      if (path) send("choose_file", { path });
      else draw();                // show the selection again, as qtprogram does
    } else if (id !== "") {
      send("choose_firmware", { id });
    }
  }

  /** Opens the user guide at the URL the engine found, or says how to build it. */
  async function userGuide() {
    const url = await engine.query("user_guide_url");
    if (url) Kit.openUrl(url);
    else {
      Kit.alert("Tag Programmer User Guide", "The Tag Programmer User Guide was not found.",
        "Build the host documentation target and try again:\ncmake --build <build-dir> --target docs", "info");
    }
  }

  /** Shows the engine's About text. */
  async function about() {
    const a = await engine.query("about");
    Kit.alert(a.title, a.text, null, "info");
  }

  // ---- Wiring -----------------------------------------------------------

  Kit.menuBar(document.getElementById("menubar"), [
    { title: "Help", items: [
      { label: "Tag Programmer User Guide", tooltip: "Open the Tag Programmer page of the user manual", action: userGuide },
      { label: "About Tag Programmer", action: about },
    ] },
    { title: "Station", items: () => [
      { label: "Attach Automatically", checked: state && state.autoAttach,
        action: () => send("set_auto_attach", { on: !(state && state.autoAttach) }) },
      { label: "Choose Base...", action: () => send("list_bases") },
      { label: "Choose Record File...", action: () => send("choose_record") },
      { label: "Refresh Firmware Releases", action: () => send("refresh_releases") },
    ] },
  ]);

  // Listen before `start`, so the first state is not missed (Tauri's listen
  // resolves asynchronously).
  Promise.all([
    engine.on("log", (e) => log.append(`${e.level} ${e.text}`)),
    engine.on("state", (s) => {
      state = s;
      Kit.setTitle(s.title);
      draw();
      showPrompt(s.prompt);
    }),
  ]).then(async () => {
    await send("set_log_level", { level: ui.level });
    await send("start");
    if (params.get("dialog") === "program") programDialog(params.get("label") || "");
  });
})();
