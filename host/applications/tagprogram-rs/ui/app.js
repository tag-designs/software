/*
 * Tag Programmer's window: draws the engine's view state and turns user
 * actions into commands and queries, per design/ui-engine-contract.md. It
 * holds no rules; every text it shows comes from the engine, except the
 * dialogs' fixed wording, which is qtprogram's.
 *
 * In a plain browser the engine is MockEngine (mock-engine.js). Preview
 * parameters: ?scenario=<name> picks a canned state (see mock-engine.js),
 * ?nopanel hides the scenario switcher, ?theme=dark|light forces a theme,
 * ?dialog=program[&label=...] opens the Program dialog, ?log opens the log.
 */
(function () {
  "use strict";

  const { el } = Kit;
  const params = Kit.setupPage("Tag Programmer");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);
  const $ = (id) => document.getElementById(id);

  let state = null;
  let shownPrompt = null;     // the id of the prompt on screen or answered
  let busy = false;           // a dialog of ours is open; ignore re-entry

  const send = (cmd, args) =>
    Promise.resolve(engine.command(cmd, args)).then((err) => {
      if (err) Kit.alert("Tag Programmer", String(err), null, "warning");
    }, (err) => Kit.alert("Tag Programmer", String(err), null, "warning"));

  // ---- Rendering --------------------------------------------------------

  function render(s) {
    state = s;
    Kit.setTitle(s.title);
    renderFirmware(s.firmware);
    renderTarget(s.tagType);
    renderChecks(s.checks, s.rowButtons);
    renderTag(s.tag);
    renderActions(s.actions);
    renderSession(s.session);
    $("batch").textContent = s.batch;
    showPrompt(s.prompt);
  }

  /** A closed <select> collapses runs of spaces; qtprogram's texts use two. */
  const keepSpaces = (text) => text.replace(/  /g, "\u00a0 ");

  function renderFirmware(fw) {
    const box = $("firmware");
    const options = [];
    if (fw.selected === "") options.push(el("option", { value: "" }, "Choose firmware"));
    for (const item of fw.items) {
      if (item.separator) options.push(el("option", { disabled: true, value: "-" }, "──────────"));
      else options.push(el("option", { value: item.id }, keepSpaces(item.text)));
    }
    box.replaceChildren(...options);
    box.value = fw.selected;
    box.disabled = !fw.enabled;
    $("release-notes").disabled = !fw.releaseNotesUrl;
  }

  function renderTarget(t) {
    const box = $("target");
    box.replaceChildren(...t.items.map((name) => el("option", { value: name }, name)));
    box.selectedIndex = t.selected === null ? -1 : t.items.indexOf(t.selected);
    box.disabled = !t.enabled;
    $("target-note").textContent = t.note;
  }

  function renderChecks(checks, rb) {
    const grid = $("checks");
    grid.replaceChildren();
    for (const c of checks) {
      const text = c.text || "—";
      const buttons = el("div", { class: "kit-status-buttons" });
      if (c.name === "Record" && rb.recordChoose) {
        buttons.append(el("button", { class: "kit-button small", type: "button",
          onclick: () => send("choose_record") }, "Choose..."));
      }
      if (c.name === "Tag state") {
        if (rb.stop.visible) {
          buttons.append(el("button", { class: "kit-button small", type: "button", disabled: !rb.stop.enabled,
            onclick: () => send("stop_tag") }, "Stop"));
        }
        if (rb.erase.visible) {
          buttons.append(el("button", { class: "kit-button small", type: "button", disabled: !rb.erase.enabled,
            onclick: confirmErase }, "Erase"));
        }
      }
      grid.append(
        el("span", { class: "kit-dot " + c.level }, "●"),
        el("span", { class: "kit-status-name" }, c.name),
        el("span", { class: "kit-status-text kit-selectable", title: c.text }, text),
        buttons);
    }
  }

  function renderTag(t) {
    $("tag-label").textContent = t.label;
    $("tag-uuid").textContent = t.uuid;
    $("tag-state").textContent = t.state;
    $("tag-firmware").textContent = t.firmware;
    $("tag-tests").textContent = t.tests;
    $("tag-clock").textContent = t.clock.text;
    $("tag-clock").classList.toggle("kit-text-warn", !!t.clock.warn);
    $("sync").disabled = !t.syncEnabled;
    const result = $("tag-result");
    result.textContent = t.result.text;
    result.classList.toggle("kit-text-ok", t.result.ok === true);
    result.classList.toggle("kit-text-fail", t.result.ok === false);
  }

  function renderActions(a) {
    for (const [key, id] of [["program", "program"], ["retest", "retest"], ["nextTag", "next-tag"], ["attach", "attach"]]) {
      const b = $(id);
      b.disabled = !a[key].enabled;
      b.title = a[key].enabled ? "" : a[key].tooltip;
    }
  }

  function renderSession(s) {
    $("step").textContent = s.stepText;
    const bar = $("progress");
    bar.hidden = s.progress === null;
    bar.classList.toggle("busy", s.progress === "busy");
    const fill = bar.firstElementChild;
    if (s.progress && typeof s.progress === "object") {
      fill.style.width = s.progress.total > 0 ? (100 * s.progress.done / s.progress.total) + "%" : "0";
    } else {
      fill.style.width = "";
    }
    $("retry").hidden = !s.retryVisible;
    $("cancel").hidden = !s.cancelVisible;
    $("cancel").textContent = s.cancelText;
  }

  // ---- Prompts ----------------------------------------------------------

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

  async function promptAutoAttach() {
    const on = await Kit.confirm("Attach Automatically?",
      "Tag Programmer can connect to each tag as soon as it finds the base, so its state and identity " +
      "are shown without pressing Attach.\n\nConnecting resets the tag, which ends a deployment that is " +
      "running. Attach automatically from now on? (Station > Attach Automatically changes this.)",
      null,
      [{ label: "No", value: false, default: true, enter: true, cancel: true }, { label: "Yes", value: true }]);
    send("answer_auto_attach", { on });
  }

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

  async function promptChooseBase(p) {
    const index = await Kit.chooseFromList("Choose a Base", p.label, p.items, p.current);
    send(index === null ? "prompt_cancel" : "choose_base", index === null ? {} : { index });
  }

  // ---- Dialogs started by the user --------------------------------------

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
   */
  async function labelAccepted(title, label) {
    const conflict = await engine.query("label_conflict", { label });
    if (!conflict) return true;
    return Kit.confirm(title, conflict,
      "Two tags with one label cannot be told apart in the record. Check the label on the board.",
      [{ label: "Cancel", value: false, cancel: true, default: true, enter: true },
       { label: "Use This Label", value: true, destructive: true }], 460);
  }

  /** qtprogram's Program dialog, built from program_preview and label_notes. */
  async function programDialog(initialLabel) {
    if (busy) return;
    busy = true;
    try {
      const pv = await engine.query("program_preview");
      if (!pv.ok) {
        await Kit.alert("Program Tag", pv.reason, null, "warning");
        return;
      }
      const input = el("input", { class: "kit-field", type: "text",
        placeholder: "The label on the board, e.g. BitTag-017", value: initialLabel || "" });
      const notes = el("div", { class: "program-notes" });
      const bullets = (items) => el("ul", {}, items.map((t) => el("li", {}, t)));
      const row = (label, value) => [el("span", { class: "kit-form-label" }, label),
        el("div", { class: "kit-form-value kit-selectable" }, value)];
      const body = el("div", {},
        el("div", { class: "kit-form program-form" },
          el("span", { class: "kit-form-label" }, "Label"), el("div", { class: "kit-form-value" }, input),
          row("Tag", pv.tag),
          row("Image", pv.image),
          row("Erase", pv.erase),
          row("Lost", bullets(pv.lost)),
          row("Kept", bullets(pv.kept))),
        notes);

      const buttons = [
        { label: "Cancel", value: null, default: false, cancel: true, enter: true },
        { label: "Erase and Program", value: "program", default: true, enabled: false },
      ];
      let seq = 0;
      const update = async () => {
        const label = input.value.trim();
        buttons[1].node.disabled = label === "";
        const mine = ++seq;
        const extra = label ? await engine.query("label_notes", { label }) : [];
        if (mine === seq) notes.textContent = pv.notes.concat(extra || []).join("\n");
      };
      input.addEventListener("input", update);
      const answer = Kit.dialog({ title: "Program Tag", body, buttons });
      update();
      setTimeout(() => input.select(), 0);
      if ((await answer) !== "program") return;
      const label = input.value.trim();
      if (await labelAccepted("Program Tag", label)) send("program", { label });
    } finally {
      busy = false;
    }
  }

  async function retestDialog() {
    const label = await Kit.promptText("Retest Tag", "Label of the tag to retest:",
      await engine.query("retest_default_label"));
    if (label === null || label.trim() === "") return;
    if (await labelAccepted("Retest Tag", label.trim())) send("retest", { label: label.trim() });
  }

  async function firmwareChosen() {
    const id = $("firmware").value;
    if (id === "other") {
      const path = await Kit.openFile({ title: "Firmware image",
        filters: [{ name: "ELF images", extensions: ["elf"] }] });
      if (path) send("choose_file", { path });
      else render(state);         // refill the combo, as qtprogram does
    } else if (id !== "") {
      send("choose_firmware", { id });
    }
  }

  async function userGuide() {
    const url = await engine.query("user_guide_url");
    if (url) Kit.openUrl(url);
    else {
      Kit.alert("Tag Programmer User Guide", "The Tag Programmer User Guide was not found.",
        "Build the host documentation target and try again:\ncmake --build <build-dir> --target docs", "info");
    }
  }

  async function about() {
    const a = await engine.query("about");
    Kit.alert(a.title, a.text, null, "info");
  }

  // ---- Wiring -----------------------------------------------------------

  Kit.menuBar($("menubar"), [
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

  $("firmware").addEventListener("change", firmwareChosen);
  $("target").addEventListener("change", () => send("choose_target", { name: $("target").value }));
  $("release-notes").addEventListener("click", () => state.firmware.releaseNotesUrl && Kit.openUrl(state.firmware.releaseNotesUrl));
  $("sync").addEventListener("click", () => send("sync_clock"));
  $("program").addEventListener("click", () => programDialog());
  $("retest").addEventListener("click", retestDialog);
  $("next-tag").addEventListener("click", () => send("next_tag"));
  $("attach").addEventListener("click", () => send("attach"));
  $("show-csv").addEventListener("click", () => state.recordPath && Kit.revealPath(state.recordPath));
  $("retry").addEventListener("click", () => send("retry_connect"));
  $("cancel").addEventListener("click", () => send("cancel_session"));

  const logText = $("log-text");
  $("log-toggle").addEventListener("click", () => {
    const on = $("log").hidden;
    $("log").hidden = !on;
    $("log-toggle").setAttribute("aria-pressed", String(on));
  });
  $("log-level").addEventListener("change", () => send("set_log_level", { level: $("log-level").value }));
  $("save-log").addEventListener("click", async () => {
    const path = await Kit.saveFile({ title: "Save Log" });
    if (path) send("save_text", { path, text: logText.value });
  });

  // Listen before `start`, so the first state is not missed (Tauri's listen
  // resolves asynchronously).
  Promise.all([
    engine.on("log", (e) => {
      const atEnd = logText.scrollTop + logText.clientHeight >= logText.scrollHeight - 4;
      logText.value += `${e.level} ${e.text}\n`;
      if (atEnd) logText.scrollTop = logText.scrollHeight;
    }),
    engine.on("state", render),
  ]).then(async () => {
    await send("set_log_level", { level: $("log-level").value });
    await send("start");
    if (params.has("log")) $("log-toggle").click();
    if (params.get("dialog") === "program") programDialog(params.get("label") || "");
  });
})();
