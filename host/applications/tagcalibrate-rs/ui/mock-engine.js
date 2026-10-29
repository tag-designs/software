/**
 * @file
 * A stand-in engine for designing the page in a plain browser. It replays
 * a recorded calibration capture (mock-capture.js: qtcalibrate's fixture
 * joined with the angles qtcalibrate computed for each sample) and sends
 * view states in the shape the engine will, with enough behaviour to click
 * through: attach, stream, start, stop, clear, tag write and read, the
 * declination and battery settings, the prompts. Not used in the app, where
 * Kit.createEngine finds Tauri first.
 *
 * The view state, as design/ui-engine-contract.md describes it (keep the
 * two, and the engine's engine/view.rs, in step):
 *
 *     title         window title
 *     link          {attached, text}: the attached tag, for the bottom bar
 *     stream        {on, enabled}: the Stream check box
 *     actions       {<command>: {visible, enabled}} for attach, detach,
 *                   start, stop, clear, tag_write, tag_read, save_capture
 *     calibration   {offset: [3 texts], mapping: [3 row texts],
 *                   field: text, accelOffset: {text, warn}}; "--" before a fit
 *     quality       {spread, worst5, hardIron, fitError, coverage,
 *                   evenness, attitude, dip}: texts; "--" before a fit
 *     sphere        {radius: µT (the fit's, or the 50 µT default before one),
 *                   points: flat [x, y, z, ...] the buffer, calibrated, µT,
 *                   newest: [x, y, z] of the sample last collected, as
 *                   plotted (calibrated once a fit exists), or null}
 *     orientation   {valid, heading: degrees as shown (declination and
 *                   battery applied), quaternion: [w, x, y, z] of the tag
 *                   model, readouts: {heading, roll, pitch, dip, field,
 *                   gravity} as texts}
 *     settings      {declination: degrees, declinationText, batteryForward}
 *     prompt        null, or a dialog the page shows once by id and answers
 *                   with `answer {id, button, index?, path?}`: alert,
 *                   chooseBase, saveFile
 *
 * Commands: ready, attach, detach, set_stream {on}, start, stop, clear,
 * tag_write, tag_read, save_capture, set_declination {degrees},
 * set_battery_forward {on}, set_log_level {level}, save_text {path, text},
 * answer, quit. Queries: menus, about, user_guide_url. Events: state, log.
 *
 * ?scenario= startup, idle, streaming (default), collecting, collected
 * (with ?percent=, default 50), orientation, prompt-base, alert.
 * ?sample=N stops the replay at sample N; ?pose=heading,pitch,roll shows a
 * fixed pose, as qtcalibrate's --orientation-pose takes it. A switcher in
 * the corner changes the scenario live unless ?nopanel is given.
 *
 * The mock holds no rules beyond what clicking through needs. Its fit is
 * the capture's final one throughout; its quality figures are samples
 * from qtcalibrate's own screenshots, except coverage, counted here over
 * 100 equal-area cells. Defines one global, `MockEngine`.
 */
(function () {
  "use strict";

  /**
   * A deep copy of plain JSON data.
   * @param {*} o
   * @returns {*}
   */
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const deg = Math.PI / 180;

  const C = window.CAPTURE;
  /** Rows: mx, my, mz, ax, ay, az, pitch, roll, model yaw, heading, dip, field. */
  const S = C.samples;
  const N = S.length;
  const { offset: V, mapping: A, field: B } = C.calibration;

  /** Each sample calibrated, A (m - V), as CompassCalibration::apply does. */
  const CAL = S.map((s) => {
    const d = [s[0] - V[0], s[1] - V[1], s[2] - V[2]];
    return A.map((row) => row[0] * d[0] + row[1] * d[1] + row[2] * d[2]);
  });

  /** For the coverage figure: the nearest of 100 equal-area cells to each sample. */
  const CELL = (() => {
    const cells = Array.from({ length: 100 }, (_, i) => {
      const z = 1 - (2 * i + 1) / 100, r = Math.sqrt(1 - z * z), a = i * Math.PI * (3 - Math.sqrt(5));
      return [r * Math.cos(a), r * Math.sin(a), z];
    });
    return CAL.map((p) => {
      const n = Math.hypot(...p);
      let best = 0, bd = -2;
      cells.forEach((c, i) => {
        const d = (c[0] * p[0] + c[1] * p[1] + c[2] * p[2]) / n;
        if (d > bd) { bd = d; best = i; }
      });
      return best;
    });
  })();

  const COMMANDS = ["attach", "detach", "start", "stop", "clear", "tag_write", "tag_read", "save_capture"];

  /** The `menus` query's answer. */
  const MENUS = {
    menus: [
      { title: "File", entries: [
        { kind: "command", id: "save_capture", label: "Save Sample Capture...", tooltip: "Save the stopped capture's samples and fit to a JSON file" },
        { kind: "item", id: "save_error_log", label: "Save Error Log...", tooltip: "Save the log to a text file" },
        { kind: "separator" },
        { kind: "item", id: "quit", label: "Quit", tooltip: "", shortcut: "quit" },
      ] },
      { title: "Tag", entries: [
        { kind: "command", id: "attach", label: "Attach", tooltip: "Find a tag base and attach to its tag" },
        { kind: "command", id: "detach", label: "Detach", tooltip: "Detach from the tag" },
        { kind: "item", id: "stream", label: "Stream", tooltip: "Read the tag's sensors continuously" },
        { kind: "separator" },
        { kind: "command", id: "start", label: "Start", tooltip: "Start collecting calibration samples" },
        { kind: "command", id: "stop", label: "Stop", tooltip: "Stop collecting" },
        { kind: "command", id: "clear", label: "Clear", tooltip: "Discard the samples and the fit" },
        { kind: "separator" },
        { kind: "command", id: "tag_write", label: "Tag Write", tooltip: "Write the fitted constants to the tag" },
        { kind: "command", id: "tag_read", label: "Tag Read", tooltip: "Read the tag's stored constants" },
      ] },
      { title: "Configuration", entries: [
        { kind: "item", id: "declination", label: "Declination...", tooltip: "The local magnetic declination, added to the heading" },
        { kind: "item", id: "battery_forward", label: "Battery Forward", tooltip: "Show the tag with its battery end forward" },
      ] },
      { title: "Log", entries: [
        { kind: "item", id: "log_level", label: "Log Level", tooltip: "" },
        { kind: "item", id: "clear_error_log", label: "Clear Log", tooltip: "Clear the log" },
      ] },
      { title: "Help", entries: [
        { kind: "item", id: "user_guide", label: "Tag Calibrator User Guide", tooltip: "Open the Tag Calibrator page of the user manual" },
        { kind: "item", id: "about", label: "About Tag Calibrator", tooltip: "Version and purpose" },
      ] },
    ],
  };

  /** Quality figures from qtcalibrate's screenshots of this capture, at 50% and 100%. */
  const QUALITY = [
    { spread: "0.27", worst5: "0.47", hardIron: "0.03", fitError: "0.25", evenness: "0.60", attitude: "1.59",
      dip: "63.1 ± 2.2", accel: "-35.7 6.7 -72.7 = 81.2 mg" },
    { spread: "0.43", worst5: "0.94", hardIron: "0.02", fitError: "0.43", evenness: "0.72", attitude: "2.06",
      dip: "63.5 ± 2.7", accel: "-29.4 -5.2 -67.1 = 73.4 mg" },
  ];

  const signed = (v) => (v >= 0 ? "+" : "") + v.toFixed(3);
  const whole = (v) => Math.round(v).toLocaleString("en-US");

  /**
   * The model's rotation, qz(yaw) qy(roll) qx(pitch), as
   * MainWindow::processCalibrationSample() turns the tag model.
   * @returns {number[]} [w, x, y, z]
   */
  function modelQuaternion(pitch, roll, yaw) {
    const q = (ax, a) => { const s = Math.sin(a * deg / 2); return [Math.cos(a * deg / 2), ax[0] * s, ax[1] * s, ax[2] * s]; };
    const mul = ([a1, b1, c1, d1], [a2, b2, c2, d2]) => [
      a1 * a2 - b1 * b2 - c1 * c2 - d1 * d2, a1 * b2 + b1 * a2 + c1 * d2 - d1 * c2,
      a1 * c2 - b1 * d2 + c1 * a2 + d1 * b2, a1 * d2 + b1 * c2 - c1 * b2 + d1 * a2];
    return mul(mul(q([0, 0, 1], yaw), q([0, 1, 0], roll)), q([1, 0, 0], pitch));
  }

  /**
   * Creates the mock.
   * @param {URLSearchParams} params The page's query parameters.
   * @returns {{command: Function, query: Function, on: Function}}
   */
  function create(params) {
    const listeners = {};
    let nextId = 1;
    // The simulated tag and session. `k` is the replay position; `kept` the
    // samples collected since Start or Clear.
    const sim = { attached: true, streaming: true, collecting: false, stopped: false, k: 0, kept: 0,
      declination: 0, batteryForward: true, pose: null, prompt: null };
    let timer = null;

    const emit = (event, payload) => (listeners[event] || []).forEach((cb) => cb(clone(payload)));
    const log = (level, text) => emit("log", { level, text });
    const ask = (p) => { sim.prompt = Object.assign({ id: nextId++ }, p); };

    /** Availability, as qtcalibrate's buttons follow the session. */
    function actions() {
      const a = sim.attached, fit = sim.kept >= 10;
      const on = {
        attach: !a, detach: a, start: a && sim.streaming && !sim.collecting, stop: sim.collecting,
        clear: sim.kept > 0 && !sim.collecting, tag_write: a && sim.stopped && fit, tag_read: a && !sim.collecting,
        save_capture: sim.stopped && sim.kept > 0,
      };
      return Object.fromEntries(COMMANDS.map((c) => [c, { visible: true, enabled: on[c] }]));
    }

    /** The calibration, quality and sphere parts of the view. */
    function fitView() {
      const n = sim.kept, first = sim.collecting || sim.stopped ? sim.k - n + 1 : 0;
      const none = "--";
      if (n < 10) {
        return {
          calibration: { offset: [none, none, none], mapping: ["-- -- --", "-- -- --", "-- -- --"], field: none,
            accelOffset: { text: none, warn: false } },
          quality: { spread: none, worst5: none, hardIron: none, fitError: none, coverage: none, evenness: none,
            attitude: none, dip: none },
          sphere: { radius: 50, points: [], newest: null },
        };
      }
      const q = QUALITY[n < N * 0.75 ? 0 : 1];
      const points = [];
      const cells = new Set();
      for (let i = first; i < first + n; i++) {
        const j = ((i % N) + N) % N;
        points.push(...CAL[j]);
        cells.add(CELL[j]);
      }
      return {
        calibration: { offset: V.map(signed), mapping: A.map((r) => r.map(signed).join(" ")), field: B.toFixed(2),
          accelOffset: { text: q.accel, warn: false } },
        quality: { spread: q.spread, worst5: q.worst5, hardIron: q.hardIron, fitError: q.fitError,
          coverage: `${cells.size}/100`, evenness: q.evenness, attitude: q.attitude, dip: q.dip },
        sphere: { radius: B, points, newest: points.slice(-3) },
      };
    }

    /** The Orientation page's part of the view, MyCompass.qml's display rules applied. */
    function orientationView() {
      if (!sim.attached || (!sim.streaming && !sim.pose)) {
        const none = "";
        return { valid: false, heading: null, quaternion: [1, 0, 0, 0],
          readouts: { heading: none, roll: none, pitch: none, dip: none, field: none, gravity: none } };
      }
      const s = S[sim.k % N];
      let [yaw, pitch, roll, heading] = [s[8], s[6], s[7], s[9]];
      if (sim.pose) { [yaw, pitch, roll] = sim.pose; heading = (360 - yaw) % 360; }
      const quaternion = modelQuaternion(pitch, roll, yaw);
      let h = heading + sim.declination, p = pitch, r = roll;
      if (!sim.batteryForward) { h += 180; p = -p; r = -r; }
      h = ((h % 360) + 360) % 360;
      return { valid: true, heading: h, quaternion,
        readouts: { heading: whole(h), roll: whole(r), pitch: whole(p), dip: whole(s[10]), field: whole(s[11]),
          gravity: whole(Math.hypot(s[3], s[4], s[5])) } };
    }

    /** The view state of the simulation. */
    function view() {
      return Object.assign({
        title: "Tag Calibrator v1.0",
        link: { attached: sim.attached, text: sim.attached ? "COMPASSTAG  ·  UUID 2035374D303150190059002F" : "" },
        stream: { on: sim.streaming, enabled: sim.attached && !sim.collecting },
        actions: actions(),
        orientation: orientationView(),
        settings: { declination: sim.declination, declinationText: sim.declination.toFixed(2),
          batteryForward: sim.batteryForward },
        prompt: sim.prompt,
      }, fitView());
    }
    const publish = () => emit("state", view());

    /** One replay step: the next sample, kept when collecting. */
    function step() {
      sim.k = (sim.k + 1) % N;
      if (sim.collecting) sim.kept = Math.min(N, sim.kept + 1);
      publish();
    }
    /** Runs or stops the replay, at the capture's 10 Hz. */
    function replay(on) {
      clearInterval(timer);
      timer = on ? setInterval(step, 100) : null;
    }

    /** Applies a scenario by name. */
    function scenario(name) {
      replay(false);
      Object.assign(sim, { attached: true, streaming: true, collecting: false, stopped: false, k: 0, kept: 0,
        pose: null, prompt: null });
      let run = true;
      if (name === "startup") { Object.assign(sim, { attached: false, streaming: false }); run = false; }
      if (name === "idle") { sim.streaming = false; run = false; }
      if (name === "collecting") sim.collecting = true;
      if (name === "collected") {
        const pct = Math.max(0, Math.min(100, Number(params.get("percent") || 50)));
        sim.k = Math.max(0, Math.round(N * pct / 100) - 1);
        sim.kept = pct ? sim.k + 1 : 0;
        if (pct >= 100) Object.assign(sim, { streaming: false, stopped: true });
        else sim.collecting = true;
        run = false;
      }
      if (name === "orientation") {
        sim.k = N - 1; sim.kept = N; sim.stopped = true; run = false;
        sim.pose = params.has("pose") ? params.get("pose").split(",").map(Number).slice(0, 3) : [45, 12, -18];
      }
      if (name === "prompt-base") {
        Object.assign(sim, { attached: false, streaming: false });
        run = false;
        ask({ kind: "chooseBase", title: "Available Bases", label: "Please select a base",
          items: ["020:004    0x483:0x3748    COMPASSTAG_BASE_V1 fw 9be2c41  (ST-LINK 0670FF484849785087201338)",
            "020:007    0x483:0x374b  (ST-LINK 066DFF545150898367194021)"] });
      }
      if (name === "alert") {
        Object.assign(sim, { attached: false, streaming: false });
        run = false;
        ask({ kind: "alert", title: "Warning", text: "No Tag Bases Found", informative: "", style: "warning" });
      }
      if (params.has("sample")) { sim.k = Math.max(0, Math.min(N - 1, Number(params.get("sample")))); run = false; }
      replay(run);
      publish();
    }

    /** One command, simulated. */
    function command(cmd, args) {
      switch (cmd) {
        case "ready": scenario(params.get("scenario") || "streaming"); return null;
        case "attach": ask({ kind: "chooseBase", title: "Available Bases", label: "Please select a base",
          items: ["020:004    0x483:0x3748    COMPASSTAG_BASE_V1 fw 9be2c41  (ST-LINK 0670FF484849785087201338)"] }); break;
        case "detach": replay(false); Object.assign(sim, { attached: false, streaming: false, collecting: false, pose: null }); break;
        case "set_stream":
          sim.streaming = !!args.on;
          sim.pose = null;
          replay(sim.streaming);
          break;
        case "start": Object.assign(sim, { collecting: true, stopped: false, kept: 0 }); log("INFO", "Calibration started"); break;
        case "stop": Object.assign(sim, { collecting: false, stopped: true }); log("INFO", `Calibration stopped: ${sim.kept} samples`); break;
        case "clear": Object.assign(sim, { kept: 0, stopped: false }); break;
        case "tag_write": log("INFO", "Calibration constants written to the tag"); break;
        case "tag_read": log("INFO", "Calibration constants read from the tag"); break;
        case "save_capture": ask({ kind: "saveFile", title: "Save Sample Capture",
          suggested: "/Users/me/qtcalibrate-samples-20261028-142600.json", filters: [{ name: "JSON", extensions: ["json"] }] }); break;
        case "set_declination": sim.declination = Number(args.degrees) || 0; break;
        case "set_battery_forward": sim.batteryForward = !!args.on; break;
        case "quit": log("INFO", "Quit (the app would close)"); break;
        case "answer": {
          const p = sim.prompt;
          if (!p || p.id !== args.id) break;
          sim.prompt = null;
          if (p.kind === "chooseBase" && args.button === "ok") {
            Object.assign(sim, { attached: true, streaming: false });
            log("INFO", "Attached to COMPASSTAG 2035374D303150190059002F");
          }
          if (p.kind === "saveFile" && args.button === "ok") log("INFO", `Saved ${sim.kept} samples to ${args.path}`);
          break;
        }
        default: break;
      }
      publish();
      return null;
    }

    if (!params.has("nopanel")) {
      const names = ["startup", "idle", "streaming", "collecting", "collected", "orientation", "prompt-base", "alert"];
      const select = Kit.el("select", { class: "kit-select", onchange: (e) => scenario(e.target.value) },
        names.map((n) => Kit.el("option", { value: n }, n)));
      select.value = params.get("scenario") || "streaming";
      document.body.append(Kit.el("div", { class: "mock-panel" }, "Scenario", select));
    }

    return {
      command: (cmd, args) => Promise.resolve(command(cmd, args || {})),
      query: (name) => Promise.resolve(clone({
        menus: MENUS,
        about: { title: "About Tag Calibrator", text: "Tag Calibrator v1.0\n\nCollect magnetometer samples from a tag " +
          "through its USB base, fit the calibration constants, store them on the tag, and check the resulting compass." },
        user_guide_url: "https://tag-designs.github.io/software/user/apps/tagcalibrate.html",
      }[name] ?? null)),
      on: (event, cb) => { (listeners[event] = listeners[event] || []).push(cb); return Promise.resolve(); },
    };
  }

  window.MockEngine = { create };
})();
