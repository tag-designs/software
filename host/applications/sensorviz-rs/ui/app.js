/**
 * @file
 * sensorViz's window: the menu bar and plot context menu, the Plot and File
 * Info tabs, the plot (plot.js), the dialogs, and the status line, drawn from
 * the engine's view state (sensorviz-rs/core/src/engine.rs) and acting only
 * through its commands. The menus are the Qt sensorviz's (mainwindow.cpp,
 * interaction.cpp), with File > Export PDF in place of Print.
 *
 * Outside the app it runs against MockEngine on the spike's fixture data:
 * ?log=imutag|compasstag|big picks the log, ?nopanel hides the switcher,
 * ?theme=dark|light forces a theme, and ?dialog=visible|sides|colors|calibration
 * opens that dialog once the log is drawn, for screenshots (in the app,
 * SENSORVIZ_PREVIEW_DIALOG does).
 */
(function () {
  "use strict";

  const params = Kit.setupPage("Tag View");
  const $ = (id) => document.getElementById(id);
  let engine = null;
  /** The engine's last view state. */
  let state = null;
  /** The last message shown, by sequence number. */
  let messageSeq = 0;
  const plot = new SvzPlot($("plot"), {
    query: (name, args) => engine.query(name, args),
    onHover: (t) => { if (t !== null) showCompass(t); },
  });

  // ---- The compass panel -----------------------------------------------------

  /** The panel's readouts, by key, and the time it last showed. */
  const readouts = {};
  let compassTime = null;
  let compassSeq = 0;
  (function buildCompass() {
    const rows = (pairs) => Kit.el("div", { class: "svz-readouts" }, pairs.flatMap(([key, label]) => {
      readouts[key] = Kit.el("output", {}, "");
      return [Kit.el("span", {}, label), readouts[key]];
    }));
    $("compass").append(
      Kit.el("div", { id: "dial" }),
      Kit.el("h3", {}, "Sensors"),
      rows([["heading", "Heading:"], ["roll", "Roll:"], ["pitch", "Pitch:"], ["dip", "Dip Angle:"], ["field", "Field:"], ["gravity", "Gravity:"]]),
      Kit.el("h3", {}, "Configuration"),
      rows([["declination", "Declination:"], ["battery", "Battery:"]]));
  })();

  /**
   * The panel at time `t` (the first sample when null), from the engine's
   * `compass` query; a reply overtaken by a newer request is dropped.
   */
  async function showCompass(t) {
    if (!state || !state.hasCompass) return;
    compassTime = t;
    const seq = ++compassSeq;
    const c = await engine.query("compass", t === null ? {} : { t });
    if (!c || seq !== compassSeq) return;
    preact.render(Kit.html`<${Kit.HeadingDial} heading=${c.heading}/>`, $("dial"));
    // Whole numbers, as the Qt panel shows them; the declination to 0.01.
    for (const key of ["heading", "roll", "pitch", "dip", "field", "gravity"]) {
      readouts[key].textContent = (Math.round(c[key]) || 0).toString();
    }
    readouts.declination.textContent = c.declination.toFixed(2);
    readouts.battery.textContent = c.batteryForward ? "forward" : "backward";
  }

  /** Sends a command to the engine. */
  const send = (cmd, args) => engine.command(cmd, args || {});

  /** The status line: the engine's. */
  function showStatus() {
    $("status").textContent = (state && state.status) || "";
  }

  /** Shows a tab. */
  function tab(which) {
    $("tab-plot").setAttribute("aria-selected", String(which === "plot"));
    $("tab-info").setAttribute("aria-selected", String(which === "info"));
    $("page-plot").hidden = which !== "plot";
    $("page-info").hidden = which !== "info";
  }
  $("tab-plot").onclick = () => tab("plot");
  $("tab-info").onclick = () => tab("info");

  // ---- Dialogs -----------------------------------------------------------

  /** The streams of the view, or none. */
  const streams = () => (state && state.view ? state.view.streams : []);
  /** The shown streams, in drawing order. */
  const visibleStreams = () => {
    if (!state || !state.view) return [];
    const byId = new Map(streams().map((s) => [s.id, s]));
    return state.view.visibleStreamIds.map((id) => byId.get(id)).filter(Boolean);
  };
  /** A stream's label with its units. */
  const named = (s) => (s.units ? `${s.label} (${s.units})` : s.label);

  /** A dialog with OK and Cancel around `body`; resolves true on OK. */
  function okCancel(title, body, width) {
    return Kit.dialog({
      title, body, width: width || 420,
      buttons: [
        { label: "Cancel", value: false, cancel: true },
        { label: "OK", value: true, default: true, enter: true },
      ],
    });
  }

  /**
   * The streams with their own visibility, for the Visible Streams list: an
   * axis group (IMUTag's accelerometer, gyroscope, magnetometer) as one block
   * where it first appears, its magnitude first and its channels indented
   * under it; streams without a group as they come.
   * @returns {Array<{s: Object, indent: boolean}>}
   */
  function listedStreams() {
    const own = streams().filter((s) => !s.derived);
    const out = [];
    const done = new Set();
    for (const s of own) {
      if (!s.group) { out.push({ s, indent: false }); continue; }
      if (done.has(s.group)) continue;
      done.add(s.group);
      const members = own.filter((m) => m.group === s.group);
      const heads = members.filter((m) => /magnitude/.test(m.id));
      const channels = members.filter((m) => !/magnitude/.test(m.id));
      if (!heads.length) { members.forEach((m) => out.push({ s: m, indent: false })); continue; }
      heads.forEach((m) => out.push({ s: m, indent: false }));
      channels.forEach((m) => out.push({ s: m, indent: true }));
    }
    return out;
  }

  /** View > Visible Streams: a check box per stream with its own visibility. */
  async function visibleStreamsDialog() {
    const boxes = listedStreams().map(({ s, indent }) => {
      const input = Kit.el("input", { type: "checkbox", checked: s.visible });
      return { id: s.id, input,
        row: Kit.el("label", { class: "svz-check-row" + (indent ? " svz-indent" : "") }, input, named(s)) };
    });
    const all = (on) => boxes.forEach((b) => { b.input.checked = on; });
    const body = Kit.el("div", {},
      Kit.el("div", { class: "svz-list" }, boxes.map((b) => b.row)),
      Kit.el("div", { class: "svz-row-buttons" },
        Kit.el("button", { class: "kit-button", type: "button", onclick: () => all(true) }, "Show All"),
        Kit.el("button", { class: "kit-button", type: "button", onclick: () => all(false) }, "Hide All")));
    if (await okCancel("Visible Streams", body)) {
      send("set_visible_streams", { ids: boxes.filter((b) => b.input.checked).map((b) => b.id) });
    }
  }

  /** View > Axis Sides: left or right per visible stream. */
  async function axisSidesDialog() {
    const rows = visibleStreams().map((s) => {
      const select = Kit.el("select", { class: "kit-select" },
        Kit.el("option", { value: "left" }, "Left"), Kit.el("option", { value: "right" }, "Right"));
      select.value = s.effectiveAxisSide;
      return { id: s.id, select, row: Kit.el("div", { class: "svz-form-row" }, Kit.el("span", {}, named(s)), select) };
    });
    if (await okCancel("Axis Sides", Kit.el("div", { class: "svz-list" }, rows.map((r) => r.row)))) {
      // The overrides are kept for every stream; only a side other than the
      // stream's default is one.
      const sides = currentSides();
      for (const r of rows) {
        const s = streams().find((x) => x.id === r.id);
        if (s && r.select.value !== s.axisSide) sides[r.id] = r.select.value;
        else delete sides[r.id];
      }
      send("set_axis_sides", { sides });
    }
  }

  /** The axis-side overrides now: streams drawn on another side than their default. */
  function currentSides() {
    const sides = {};
    for (const s of streams()) if (s.effectiveAxisSide !== s.axisSide) sides[s.id] = s.effectiveAxisSide;
    return sides;
  }

  /** The color overrides now: streams drawn in another color than their default. */
  function currentColors() {
    const colors = {};
    for (const s of streams()) if (s.effectiveColor !== s.color) colors[s.id] = s.effectiveColor;
    return colors;
  }

  /** View > Colors: a color per visible stream, and Defaults. */
  async function colorsDialog() {
    const rows = visibleStreams().map((s) => {
      const input = Kit.el("input", { type: "color", value: s.effectiveColor });
      return { s, input, row: Kit.el("div", { class: "svz-form-row" }, Kit.el("span", {}, named(s)), input) };
    });
    const defaults = () => rows.forEach((r) => { r.input.value = r.s.color; });
    const body = Kit.el("div", {}, Kit.el("div", { class: "svz-list" }, rows.map((r) => r.row)),
      Kit.el("div", { class: "svz-row-buttons" },
        Kit.el("button", { class: "kit-button", type: "button", onclick: defaults }, "Defaults")));
    if (await okCancel("Stream Colors", body)) {
      const colors = currentColors();
      for (const r of rows) {
        if (r.input.value.toLowerCase() === r.s.color.toLowerCase()) delete colors[r.s.id];
        else colors[r.s.id] = r.input.value.toLowerCase();
      }
      send("set_colors", { colors });
    }
  }

  /** A number field. */
  const numberField = (value, step) =>
    Kit.el("input", { class: "kit-field", type: "number", step: String(step || "any"), value: String(value) });

  /** View > Ranges > a stream: its y range. */
  async function rangeDialog(s) {
    const r = await engine.query("range", { id: s.id });
    if (!r) return;
    const span = Math.max(1, r.upper - r.lower);
    const lower = numberField(+r.lower.toFixed(2), span / 100), upper = numberField(+r.upper.toFixed(2), span / 100);
    const units = s.units ? ` ${s.units}` : "";
    const body = Kit.el("div", { class: "svz-form" },
      Kit.el("label", {}, "Minimum"), Kit.el("span", {}, lower, units),
      Kit.el("label", {}, "Maximum"), Kit.el("span", {}, upper, units));
    if (await okCancel(`${s.label} Range`, body, 360)) {
      send("set_stream_range", { id: s.id, lower: parseFloat(lower.value), upper: parseFloat(upper.value) });
    }
  }

  /** A transform parameter's number, in its spec's limits and decimals. */
  async function parameterDialog(p) {
    const field = numberField(p.value.toFixed(p.decimals), Math.pow(10, -p.decimals));
    field.min = String(p.minimum);
    field.max = String(p.maximum);
    const body = Kit.el("div", {}, Kit.el("div", { style: { marginBottom: "8px" } }, p.prompt), field);
    if (await okCancel(p.title, body, 360)) {
      const v = parseFloat(field.value);
      if (!Number.isNaN(v)) send("set_parameter", { id: p.id, value: Math.min(p.maximum, Math.max(p.minimum, v)) });
    }
  }

  /** Configuration > Graph Title. */
  async function titleDialog() {
    const text = await Kit.promptText("Graph Title", "Title", state.view.title);
    if (text !== null) send("set_title", { text });
  }

  /** Configuration > UTC Offset: hours, -12 to 12. */
  async function utcDialog() {
    const field = numberField(state.view.utcOffsetHours, 1);
    field.min = "-12";
    field.max = "12";
    const body = Kit.el("div", {}, Kit.el("div", { style: { marginBottom: "8px" } }, "Offset from UTC"), field);
    if (await okCancel("UTC Offset", body, 300)) {
      const hours = parseInt(field.value, 10);
      if (!Number.isNaN(hours)) send("set_utc_offset", { hours: Math.max(-12, Math.min(12, hours)) });
    }
  }

  /** View > Calibration Constants: the log's compass calibration, read only. */
  function calibrationDialog() {
    const c = state.calibration;
    const fmt = (x) => x.toFixed(6);
    const rows = [
      ["Hard iron (µT)", c.hardIron.map(fmt).join("   ")],
      ["Soft iron", [0, 3, 6].map((i) => c.softIron.slice(i, i + 3).map(fmt).join("   ")).join("\n")],
      ["Accelerometer offset (mg)", c.accelOffset.map(fmt).join("   ")],
      ["Epoch", String(c.epoch)],
    ];
    if (c.warning) rows.push(["Warning", c.warning]);
    Kit.dialog({
      title: "Calibration Constants", width: 520,
      body: Kit.el("div", { class: "svz-form" }, rows.flatMap(([k, v]) => [Kit.el("label", {}, k), Kit.el("pre", { class: "kit-selectable" }, v)])),
      buttons: [{ label: "OK", value: true, default: true, enter: true, cancel: true }],
    });
  }

  // ---- File actions ------------------------------------------------------

  const logFilters = [{ name: "SQLite Logs", extensions: ["db3"] }, { name: "All Files", extensions: ["*"] }];
  const prefFilters = [{ name: "sensorViz Preferences", extensions: ["json"] }];
  const dialogDir = () => (state && state.dialogDir) || undefined;
  const joinDir = (dir, name) => (dir ? `${dir.replace(/[\\/]$/, "")}/${name}` : name);

  async function loadLog() {
    const path = await Kit.openFile({ title: "Open Sensor Log", filters: logFilters, defaultPath: dialogDir() });
    if (path) send("open", { path });
  }
  async function loadPreferences() {
    const path = await Kit.openFile({ title: "Load sensorViz Preferences", filters: prefFilters, defaultPath: dialogDir() });
    if (path) send("load_preferences", { path });
  }
  async function storePreferences() {
    let path = await Kit.saveFile({ title: "Save sensorViz Preferences", filters: prefFilters,
      defaultPath: joinDir(dialogDir(), "sensorviz-preferences.json") });
    if (!path) return;
    if (!/\.[^\\/]+$/.test(path)) path += ".json";
    send("store_preferences", { path });
  }

  /** The bytes of an ArrayBuffer as base64, in chunks the call stack allows. */
  function base64(buffer) {
    const bytes = new Uint8Array(buffer);
    let text = "";
    for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(text);
  }

  /** File > Export PDF: the current view, as plot.js draws it for print. */
  async function exportPdf() {
    if (!state || !state.spec) return;
    const name = `${(state.fileName || "sensorviz").replace(/\.db3$/, "")}.pdf`;
    if (!Kit.inApp()) {
      (await plot.exportPdf()).save(name);
      return;
    }
    const path = await Kit.saveFile({ title: "Export PDF", filters: [{ name: "PDF", extensions: ["pdf"] }],
      defaultPath: joinDir(dialogDir(), name) });
    if (!path) return;
    const doc = await plot.exportPdf();
    send("save_file", { path, base64: base64(doc.output("arraybuffer")) });
  }

  async function about() {
    const a = await engine.query("about", {});
    Kit.alert("About Tag View", "Tag View", `sensorViz in Rust: the tag SQLite log viewer, version ${a ? a.version : ""}.`);
  }
  async function userGuide() {
    const a = await engine.query("about", {});
    if (a && a.userGuide) Kit.openUrl(a.userGuide);
  }

  // ---- Menus -------------------------------------------------------------

  /** The View menu, for the menu bar and the plot's context menu. */
  function viewItems() {
    const has = !!(state && state.hasLog);
    const shown = visibleStreams();
    const cursors = !!(state && state.view && state.view.cursorsVisible);
    const items = [
      { label: "Visible Streams...", disabled: !has, action: visibleStreamsDialog },
      { label: "Axis Sides...", disabled: !shown.length, action: axisSidesDialog },
      { label: "Colors...", disabled: !shown.length, action: colorsDialog },
      { label: "Ranges", disabled: !shown.length,
        items: () => shown.map((s) => ({ label: `${s.label} Range...`, tooltip: `Set the displayed y-axis range for ${s.label}.`, action: () => rangeDialog(s) })) },
      "-",
      { label: "Reset Zoom", disabled: !has, action: () => { send("reset_zoom"); plot.resetZoom(); } },
      { label: "Zoom to Cursors", disabled: !has || !cursors, action: () => plot.zoomToCursors() },
      { label: "Show Cursors", disabled: !has, checked: cursors, action: () => {
        send("set_cursors_visible", { on: !cursors });
        plot.setCursorsVisible(!cursors);
      } },
      { label: "Break Lines at Gaps", disabled: !has, checked: !!(state && state.view && state.view.breakAtGaps),
        action: () => send("set_break_at_gaps", { on: !state.view.breakAtGaps }) },
    ];
    if (state && state.hasCalibration) items.push("-", { label: "Calibration Constants", action: calibrationDialog });
    return items;
  }

  /** The Configuration menu, its transform items built from the parameters' specs. */
  function configurationItems() {
    const has = !!(state && state.hasLog);
    const v = state && state.view;
    const items = [
      { label: "Graph Title...", disabled: !has, action: titleDialog },
      { label: "Show Graph Title", disabled: !has, checked: !!(v && v.titleVisible), action: () => send("set_title_visible", { on: !v.titleVisible }) },
      "-",
      { label: v ? `UTC Offset... (${v.utcOffsetHours})` : "UTC Offset...", disabled: !has || state.timeDomain !== "epoch", action: utcDialog },
    ];
    const ps = (state && state.parameters) || [];
    if (ps.length) {
      items.push("-");
      for (const p of ps) {
        if (p.kind === "toggle") {
          items.push({ label: p.title, checked: p.value !== 0, action: () => send("set_parameter", { id: p.id, value: p.value !== 0 ? 0 : 1 }) });
        } else {
          items.push({ label: `${p.title}... (${p.value.toFixed(p.decimals)} ${p.units})`, action: () => parameterDialog(p) });
        }
      }
    }
    return items;
  }

  Kit.menuBar($("menubar"), [
    { title: "File", items: () => [
      { label: "Load...", action: loadLog },
      { label: "Preferences", items: () => [
        { label: "Load...", action: loadPreferences },
        { label: "Store...", disabled: !(state && state.hasLog), action: storePreferences },
        "-",
        { label: "Load Defaults", disabled: !(state && state.hasLog), action: () => send("load_default_preferences") },
      ] },
      "-",
      { label: "Export PDF...", disabled: !(state && state.spec), action: exportPdf },
    ] },
    { title: "View", items: viewItems },
    { title: "Configuration", items: configurationItems },
    { title: "Help", items: () => [
      { label: "Tag View User Guide", action: userGuide },
      "-",
      { label: "About", action: about },
    ] },
  ]);

  $("plot").addEventListener("contextmenu", (ev) => {
    if (!state || !state.hasLog) return;
    Kit.contextMenu(ev, [
      { label: "File", items: [{ label: "Load...", action: loadLog }, { label: "About", action: about }] },
      { label: "View", items: viewItems },
      { label: "Configuration", items: configurationItems },
    ]);
  });

  // ---- The engine ----------------------------------------------------------

  /** Draws a new view state. */
  function apply(s) {
    const old = state;
    state = s;
    if (!old || JSON.stringify(old.spec) !== JSON.stringify(s.spec)) plot.setSpec(s.spec);
    if (s.view) plot.cursors.visible = s.view.cursorsVisible;
    $("info").textContent = s.fileInfo || "";
    $("compass").hidden = !s.hasCompass;
    // A new log starts the panel at its first sample; a changed declination
    // or battery direction redraws it at the same time.
    if (s.hasCompass && (!old || old.path !== s.path)) showCompass(null);
    else if (s.hasCompass && JSON.stringify(old.parameters) !== JSON.stringify(s.parameters)) showCompass(compassTime);
    $("empty").hidden = !!s.hasLog;
    Kit.setTitle(s.fileName ? `Tag View - ${s.fileName}` : "Tag View");
    showStatus();
    if (s.message && s.message.seq > messageSeq) {
      messageSeq = s.message.seq;
      Kit.alert(s.message.title, s.message.text, null, "warning");
    }
  }

  /** Starts against the engine, or the mock and its fixture data in a browser. */
  async function start() {
    let mock = null;
    if (!Kit.inApp()) {
      for (const src of ["mock/data-imutag.js", "mock/data-compasstag.js", "mock-engine.js"]) {
        await new Promise((resolve) => {
          const script = Kit.el("script", { src });
          script.onload = script.onerror = resolve;
          document.body.append(script);
        });
      }
      mock = window.MockEngine ? MockEngine.create(params) : null;
    }
    engine = Kit.createEngine(mock);
    await engine.on("state", apply);
    await send("ready");
    const dialogs = { visible: visibleStreamsDialog, sides: axisSidesDialog, colors: colorsDialog, calibration: calibrationDialog };
    const preview = Kit.inApp() ? ((await engine.query("about", {})) || {}).previewDialog : params.get("dialog");
    if (dialogs[preview]) setTimeout(dialogs[preview], 500);
    // Browser preview: ?export=chart shows the PDF's chart image in place of
    // the window, to check how the print rendering looks.
    if (!Kit.inApp() && params.get("export") === "chart") {
      setTimeout(async () => {
        await plot.exportPdf();
        document.body.replaceChildren(Kit.el("img", { src: plot.lastPrintImage, style: { width: "100%" } }));
      }, 500);
    }
  }
  start();
})();
