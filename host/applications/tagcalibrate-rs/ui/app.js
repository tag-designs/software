/**
 * @file
 * Tag Calibrator's window: draws the engine's view state and turns user
 * actions into commands. It holds no rules: the menus come from the
 * engine's `menus` query, which commands are enabled from the state's
 * `actions`, every figure and text from the engine. The view state and the
 * commands are described in mock-engine.js until the engine exists.
 *
 * In a plain browser the engine is MockEngine (mock-engine.js). Preview
 * parameters: ?scenario=<name> picks a canned state (see mock-engine.js),
 * ?nopanel hides the scenario switcher, ?theme=dark|light forces a theme,
 * ?page=calibrate|orientation|log picks the tab (Calibrate by default).
 *
 * Flow: the engine sends its whole view state (event `state`) after every
 * change, and draw() renders the window from it with Preact, the kit's
 * components and the graphics in views.js. Every dialog the engine needs is
 * a prompt in the state (state.prompt); the page shows each once, by id,
 * and answers with `answer {id, button, index?, path?}`. What the page
 * keeps itself: the selected tab, the log text and level, and the attitude
 * view's screen direction and both views' cameras.
 */
(function () {
  "use strict";

  const { html, render } = Kit;
  const { useLayoutEffect, useRef } = Kit.hooks;
  const { Button, Group, Select, LogPane } = Kit.ui;
  const { SphereView, TagView, HeadingDial } = CalViews;
  const params = Kit.setupPage("Tag Calibrator");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);

  let state = null;
  let menus = null;            // the `menus` query's answer
  let shownPrompt = null;      // the id of the prompt on screen or answered
  /** What the page keeps itself: the tab shown, the log level, the screen direction. */
  const ui = {
    page: ["calibrate", "orientation", "log"].includes(params.get("page")) ? params.get("page") : "calibrate",
    level: "INFO",
    direction: 0,
  };
  const log = Kit.logModel();

  /**
   * Sends a command; an error string in the answer, or a rejected invoke,
   * is shown in an alert.
   * @param {string} cmd Command name.
   * @param {Object} [args] Its arguments.
   * @returns {Promise<void>}
   */
  const send = (cmd, args) =>
    Promise.resolve(engine.command(cmd, args)).then((err) => {
      if (err) Kit.alert("Tag Calibrator", String(err), null, "warning");
    }, (err) => Kit.alert("Tag Calibrator", String(err), null, "warning"));

  /**
   * Answers the prompt with `id`.
   * @param {number} id The prompt's id.
   * @param {string} button "ok" or "cancel".
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
    ["calibrate", "Calibrate", "Collect samples and fit the magnetometer calibration"],
    ["orientation", "Orientation", "The compass and attitude the calibration gives"],
    ["log", "Log", "Messages from Tag Calibrator and the tag"],
  ];

  /**
   * The whole window below the menu bar: tabs, the three pages (all kept
   * mounted, so the 3D views keep their cameras and the log its text) and
   * the Tag Connection bar.
   * @param {{s: Object}} props The engine's view state.
   * @returns {Object} A Preact element.
   */
  function Window({ s }) {
    return html`
      <div class="tabs" role="tablist">
        ${TABS.map(([page, label, tooltip]) => html`<button type="button" role="tab" title=${tooltip}
          aria-selected=${String(ui.page === page)} onClick=${() => { ui.page = page; draw(); }}>${label}</button>`)}
      </div>
      <div class="pages" onContextMenu=${popup}>
        <section id="page-calibrate" class="page" hidden=${ui.page !== "calibrate"}>
          <${CalibratePage} s=${s}/>
        </section>
        <section id="page-orientation" class="page" hidden=${ui.page !== "orientation"}>
          <${OrientationPage} s=${s}/>
        </section>
        <section id="page-log" class="page" hidden=${ui.page !== "log"}>
          <${LogPane} class="log-page" log=${log} level=${ui.level} onLevel=${setLevel} onSave=${saveLog}
            onClear=${() => log.clear()}/>
        </section>
      </div>
      <${Connection} s=${s}/>`;
  }

  /** The quality figures: state key, heading, tooltip; two rows of four. */
  const QUALITY = [
    [
      ["spread", "Spread", "Robust width of the fit residual, as a percentage of the field"],
      ["worst5", "Worst 5%", "95th percentile of the absolute fit residual, as a percentage of the field. " +
        "Well above Spread, a few samples are bad."],
      ["hardIron", "Hard Iron", "Hard iron the fit did not remove, in µT. Read it beside Coverage."],
      ["fitError", "Fit Error", "Error of the fit, as a percentage of the field"],
    ],
    [
      ["coverage", "Coverage", "Patches of the sphere with samples. Low: keep turning the tag."],
      ["evenness", "Evenness", "How evenly the samples cover the sphere. Low: tip the tag rather than only spinning it."],
      ["attitude", "Attitude", "Diversity of attitude about the field. Low: roll the tag about the field as well."],
      ["dip", "Dip", "Dip angle, mean ± spread, in degrees: the one figure the fit does not optimise, " +
        "so it cannot flatter it."],
    ],
  ];

  /**
   * The Calibrate page: the sample sphere and its quality figures, the
   * fitted constants, and the collection controls.
   * @param {{s: Object}} props The view state.
   * @returns {Object} A Preact element.
   */
  function CalibratePage({ s }) {
    const c = s.calibration;
    const a = s.actions;
    const act = (cmd, label, tooltip) => html`<${Button} disabled=${!a[cmd].enabled} hidden=${!a[cmd].visible}
      title=${tooltip} onClick=${() => send(cmd)}>${label}<//>`;
    return html`<div class="cal-page">
      <${Group} class="magnetometer" title="Magnetometer">
        <${SphereView} sphere=${s.sphere}/>
        <div class="quality">
          ${QUALITY.map((row) => row.map(([key, label, tooltip]) => html`<div class="figure" title=${tooltip}>
            <span class="figure-label">${label}</span><span class="figure-value kit-selectable">${s.quality[key]}</span>
          </div>`))}
        </div>
      <//>
      <div class="cal-side">
        <${Group} class="constants" title="Calibration">
          <${Constant} title="Magnetic Offset" lines=${c.offset}
            tooltip="Hard-iron offset V, µT: subtracted from each reading"/>
          <${Constant} title="Magnetic Mapping" lines=${c.mapping}
            tooltip="Soft-iron matrix A: maps the offset reading onto a sphere"/>
          <${Constant} title="Magnetic Field" lines=${[c.field]} tooltip="Fitted field magnitude B, µT"/>
          <${Constant} title="Accelerometer Offset" lines=${[c.accelOffset.text]} warn=${c.accelOffset.warn}
            tooltip="Accelerometer zero-g offset, milli-g, and its magnitude"/>
        <//>
        <${Group} class="controls" title="Controls">
          ${act("start", "Start", "Start collecting calibration samples")}
          ${act("stop", "Stop", "Stop collecting")}
          ${act("clear", "Clear", "Discard the samples and the fit")}
          <div class="kit-spacer"></div>
          ${act("tag_write", "Tag Write", "Write the fitted constants to the tag")}
          ${act("tag_read", "Tag Read", "Read the tag's stored constants")}
        <//>
      </div>
    </div>`;
  }

  /**
   * One fitted constant: a small title over its lines in a fixed-width box.
   * @param {Object} props
   * @param {string} props.title
   * @param {string[]} props.lines The engine's texts, one per line.
   * @param {boolean} [props.warn] Shown in the warning colour.
   * @param {string} props.tooltip
   * @returns {Object} A Preact element.
   */
  const Constant = ({ title, lines, warn, tooltip }) => html`<div class="constant" title=${tooltip}>
    <div class="constant-title">${title}</div>
    <div class=${"constant-box kit-selectable" + (warn ? " warn" : "")}>${lines.join("\n")}</div>
  </div>`;

  /** The Sensors readouts: state key, label, tooltip. */
  const READOUTS = [
    ["heading", "Heading", "Compass heading, degrees, declination and battery direction applied"],
    ["roll", "Roll", "Degrees"],
    ["pitch", "Pitch", "Degrees"],
    ["dip", "Dip Angle", "Angle of the field below the horizontal, degrees"],
    ["field", "Field", "Field magnitude, µT"],
    ["gravity", "Gravity", "Acceleration magnitude, milli-g"],
  ];

  /**
   * The Orientation page: the heading indicator, readouts and display
   * settings on the left; the tag's attitude on the right.
   * @param {{s: Object}} props The view state.
   * @returns {Object} A Preact element.
   */
  function OrientationPage({ s }) {
    const o = s.orientation;
    const set = s.settings;
    return html`<div class="ori-page">
      <${Group} class="compass" title="Compass">
        <${HeadingDial} heading=${o.heading}/>
        <div class="ori-forms">
          <div class="ori-title">Sensors</div>
          <div class="kit-form readouts">
            ${READOUTS.map(([key, label, tooltip]) => html`<span class="kit-form-label">${label}</span><span
              class="readout kit-selectable" title=${tooltip}>${o.readouts[key]}</span>`)}
          </div>
          <div class="ori-title">Configuration</div>
          <div class="kit-form">
            <span class="kit-form-label">Declination</span>
            <span><${NumberField} value=${set.declination} decimals=${2} step=${0.5} min=${-180} max=${180}
              tooltip="Local magnetic declination, degrees east, added to the heading"
              onValue=${(v) => send("set_declination", { degrees: v })}/><span class="unit">°</span></span>
            <span class="kit-form-label">Battery</span>
            <${Select} title="Which end of the tag is forward" options=${["forward", "backward"]}
              value=${set.batteryForward ? "forward" : "backward"}
              onChange=${(v) => send("set_battery_forward", { on: v === "forward" })}/>
          </div>
        </div>
      <//>
      <${Group} class="attitude" title="Attitude">
        <${TagView} quaternion=${o.quaternion} batteryForward=${set.batteryForward} direction=${ui.direction}/>
        <div class="direction">
          <span>Screen Direction</span>
          <${NumberField} value=${ui.direction} decimals=${0} step=${15} min=${-360} max=${360}
            tooltip="Turn the view about the vertical, degrees"
            onValue=${(v) => { ui.direction = v; draw(); }}/><span class="unit">°</span>
        </div>
      <//>
    </div>`;
  }

  /**
   * A number field written from `value` only while it does not have focus,
   * so a new state never replaces what the user is typing.
   * @param {Object} props
   * @param {number} props.value
   * @param {number} props.decimals Shown.
   * @param {number} props.step Of the arrows.
   * @param {number} props.min
   * @param {number} props.max
   * @param {string} props.tooltip
   * @param {Function} props.onValue Called with a number on each change.
   * @returns {Object} A Preact element.
   */
  function NumberField({ value, decimals, step, min, max, tooltip, onValue }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
      if (document.activeElement !== ref.current) ref.current.value = Number(value).toFixed(decimals);
    });
    return html`<input ref=${ref} class="kit-field number" type="number" step=${step} min=${min} max=${max}
      title=${tooltip} onChange=${(ev) => {
        const v = parseFloat(ev.currentTarget.value);
        if (!Number.isNaN(v)) onValue(v);
      }}/>`;
  }

  /**
   * The Tag Connection bar: Attach, Detach, the attached tag, Stream.
   * @param {{s: Object}} props The view state.
   * @returns {Object} A Preact element.
   */
  function Connection({ s }) {
    const a = s.actions;
    return html`<${Group} class="connection" title="Tag Connection">
      <div class="connection-row">
        <${Button} disabled=${!a.attach.enabled} title="Find a tag base and attach to its tag"
          onClick=${() => send("attach")}>Attach<//>
        <${Button} disabled=${!a.detach.enabled} title="Detach from the tag" onClick=${() => send("detach")}>Detach<//>
        <span class="link kit-selectable"><span class=${s.link.attached ? "dot-on" : "dot-off"}>●</span>
          ${s.link.attached ? s.link.text : "Detached"}</span>
        <span class="kit-spacer"></span>
        <label class=${"stream" + (s.stream.enabled ? "" : " disabled")} title="Read the tag's sensors continuously">
          <input type="checkbox" checked=${s.stream.on} disabled=${!s.stream.enabled}
            onClick=${(ev) => send("set_stream", { on: ev.currentTarget.checked })}/> Stream
        </label>
      </div>
    <//>`;
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
      case "alert":
        await Kit.alert(p.title, p.text, p.informative || null, p.style === "info" ? "info" : "warning");
        return answer(id, "ok");
      case "chooseBase": {
        const index = await Kit.chooseFromList(p.title, p.label, p.items, 0);
        return index === null ? answer(id, "cancel") : answer(id, "ok", { index });
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
      case "stream":
        item.checked = !!(state && state.stream.on);
        item.disabled = !(state && state.stream.enabled);
        item.action = () => send("set_stream", { on: !state.stream.on });
        break;
      case "declination":
        if (state) item.label = `Declination... (${state.settings.declinationText}°)`;
        item.action = declination;
        break;
      case "battery_forward":
        item.checked = !!(state && state.settings.batteryForward);
        item.action = () => send("set_battery_forward", { on: !state.settings.batteryForward });
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
   * The popup menu: the menu bar's menus as submenus.
   * @param {MouseEvent} ev The contextmenu event.
   */
  function popup(ev) {
    if (!menus || ev.target.closest(".kit-log")) return;
    Kit.contextMenu(ev, menus.menus.map((m) => ({ label: m.title, items: () => m.entries.map(menuItem) })));
  }

  /** Configuration ▸ Declination: asks for the angle, as qtcalibrate does. */
  async function declination() {
    const text = await Kit.promptText("Declination", "Declination angle (degrees east)",
      state ? state.settings.declinationText : "0.00");
    if (text === null) return;
    const v = parseFloat(text);
    if (Number.isNaN(v)) Kit.alert("Declination", `"${text}" is not a number.`, null, "warning");
    else send("set_declination", { degrees: v });
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
    const path = await Kit.saveFile({ title: "Save Log", filters: [{ name: "Text", extensions: ["txt"] }] });
    if (path) send("save_text", { path, text: log.text() });
  }

  /** Opens the user guide at the URL the engine found. */
  async function userGuide() {
    const url = await engine.query("user_guide_url");
    if (url) Kit.openUrl(url);
    else Kit.alert("Tag Calibrator User Guide", "The Tag Calibrator User Guide was not found.", null, "info");
  }

  /** Shows the engine's About text. */
  async function about() {
    const a = await engine.query("about");
    Kit.alert(a.title, a.text, null, "info");
  }

  // ---- Wiring -----------------------------------------------------------

  // The menus' standard shortcut: Ctrl or Cmd with Q.
  document.addEventListener("keydown", (ev) => {
    if (!(ev.metaKey || ev.ctrlKey) || ev.altKey || document.querySelector(".kit-backdrop")) return;
    if (ev.key.toLowerCase() === "q") { ev.preventDefault(); send("quit"); }
  });

  Promise.all([
    engine.query("menus").then((m) => { menus = m; Kit.menuBar(document.getElementById("menubar"), kitMenus()); }),
    engine.on("log", (e) => log.append(`${e.level} ${e.text}`)),
    engine.on("state", (s) => {
      state = s;
      Kit.setTitle(s.title);
      draw();
      showPrompt(s.prompt);
    }),
  ]).then(() => send("set_log_level", { level: ui.level }))
    .then(() => send("ready"));
})();
