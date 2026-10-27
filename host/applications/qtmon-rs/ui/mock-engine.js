/**
 * @file
 * A stand-in engine for designing the page in a plain browser: view states
 * in the shape of design/ui-engine-contract.md, and enough behaviour to
 * click through (attach, start, stop, erase, download, the prompts). Not
 * used in the app, where Kit.createEngine finds Tauri first.
 *
 * ?scenario= idle (default), detached, running, finished, erasing,
 * downloading, prompt-start, prompt-unsaved, prompt-base, alert. A switcher
 * in the corner changes it live unless ?nopanel is given.
 *
 * The mock holds no rules beyond what clicking through needs: its states,
 * menus and answers are hand-written samples of the contract's shapes. When
 * the contract changes, update them here. Defines one global, `MockEngine`.
 */
(function () {
  "use strict";

  /**
   * A deep copy of plain JSON data.
   * @param {*} o
   * @returns {*}
   */
  const clone = (o) => JSON.parse(JSON.stringify(o));

  const COMMANDS = ["attach", "detach", "read", "start", "stop", "sync", "test", "erase", "download", "open", "save", "reset"];

  /** The `menus` query's answer: the core's menu table. */
  const MENUS = {
    menus: [
      { title: "File", entries: [
        { kind: "item", id: "new_configuration", label: "New Configuration", tooltip: "" },
        { kind: "command", id: "open", label: "Open Configuration...", tooltip: "Open a configuration file for editing", shortcut: "open" },
        { kind: "item", id: "open_recent", label: "Open Recent", tooltip: "" },
        { kind: "command", id: "save", label: "Save Configuration...", tooltip: "Save the configuration to a file", shortcut: "save" },
        { kind: "command", id: "reset", label: "Reset to Defaults", tooltip: "Replace the configuration's values with its tag type's defaults" },
        { kind: "separator" },
        { kind: "command", id: "download", label: "Download Data...", tooltip: "Download the tag's data log to a file" },
        { kind: "item", id: "save_error_log", label: "Save Error Log...", tooltip: "Save the error log to a text file" },
        { kind: "separator" },
        { kind: "item", id: "quit", label: "Quit", tooltip: "", shortcut: "quit" },
      ] },
      { title: "Tag", entries: [
        { kind: "command", id: "attach", label: "Attach", tooltip: "Find a tag base and attach to its tag" },
        { kind: "command", id: "detach", label: "Detach", tooltip: "Detach from the tag" },
        { kind: "separator" },
        { kind: "command", id: "read", label: "Read Configuration", tooltip: "Read the tag's configuration" },
        { kind: "command", id: "start", label: "Start", tooltip: "Write the configuration to the tag and start it" },
        { kind: "command", id: "stop", label: "Stop", tooltip: "Stop the tag" },
        { kind: "separator" },
        { kind: "command", id: "sync", label: "Synchronize Clock", tooltip: "Set the tag's clock from this computer's" },
        { kind: "command", id: "test", label: "Self-Test", tooltip: "Run the tag's self-tests" },
        { kind: "separator" },
        { kind: "command", id: "erase", label: "Erase...", tooltip: "Erase the tag's data and return it to IDLE" },
      ] },
      { title: "Log", entries: [
        { kind: "item", id: "log_level", label: "Log Level", tooltip: "" },
        { kind: "item", id: "clear_error_log", label: "Clear Error Log", tooltip: "Clear the error log" },
      ] },
      { title: "Help", entries: [
        { kind: "item", id: "user_guide", label: "Tag Monitor User Guide", tooltip: "Open the Tag Monitor page of the user manual" },
        { kind: "item", id: "about", label: "About Tag Monitor", tooltip: "Version and purpose" },
      ] },
    ],
    newConfigurationTypes: ["BITTAG", "PRESTAG", "COMPASSTAG", "IMUTAG", "UIUCTAG"],
  };

  const INFO = {
    tagType: "COMPASSTAG", board: "CompassTagv1", firmware: "CompassTagv1, Firmware version 1",
    gitHash: "acd3167", gitUrl: "git@github.com:tag-designs/software.git", uuid: "203633324B425006003E005D",
    flash: "256KB", flashExt: "4MB", buildDate: "Aug 26 2026 : 13:43:36", sourcePath: "/embedded/tags/CompassTag",
  };

  /**
   * The Configuration form for BITTAG defaults, as the engine sends it
   * (state.config.panels). Regenerate it with
   * `cargo run -p qtmon-core --example form_view -- BITTAG`.
   */
  const FORM = [{"controls":[{"choices":[{"id":0,"text":"Immediately","tooltip":"Start data collection as soon as possible."},{"id":1,"text":"On","tooltip":"Start data collection at a specific time."}],"enabled":true,"group":"Start","id":"start_mode","kind":"radio","label":"","tooltip":"Set data collection starting conditions.","value":0},{"enabled":true,"group":"Start","id":"start","kind":"datetime","label":"","max":253402300799,"min":1791460980,"tooltip":"Programmed date/time to start data collection (UTC, whole hours)","value":1791464400},{"choices":[{"id":0,"text":"Indefinitely","tooltip":"Collect data for as long as possible, until either battery or storage runs out."},{"id":1,"text":"Until","tooltip":"Stop collecting data at a specific time."}],"enabled":true,"group":"Run","id":"run_mode","kind":"radio","label":"","tooltip":"Set data collection ending conditions.","value":0},{"enabled":true,"group":"Run","id":"end","kind":"datetime","label":"","max":253402300799,"min":1791464400,"tooltip":"Programmed date/time to halt data collection (UTC, whole hours)","value":1791464400},{"enabled":true,"group":"Hibernation","id":"hib0.in_use","kind":"check","label":"","text":"In use","tooltip":"Hibernate between the start and end below. Unchecked, the row is not used.","value":false},{"enabled":false,"group":"Hibernation","id":"hib0.start","kind":"datetime","label":"Start","max":253402300799,"min":-6857222400,"tooltip":"Start hibernating at this date/time (UTC, whole hours)","value":0},{"enabled":false,"group":"Hibernation","id":"hib0.end","kind":"datetime","label":"End","max":253402300799,"min":-6857222400,"tooltip":"Stop hibernating at this date/time (UTC, whole hours). The tag checks once an hour, on the hour, whether to stop hibernating.","value":0},{"enabled":true,"group":"Hibernation","id":"hib1.in_use","kind":"check","label":"","text":"In use","tooltip":"Hibernate between the start and end below. Unchecked, the row is not used.","value":false},{"enabled":false,"group":"Hibernation","id":"hib1.start","kind":"datetime","label":"Start","max":253402300799,"min":-6857222400,"tooltip":"Start hibernating at this date/time (UTC, whole hours)","value":0},{"enabled":false,"group":"Hibernation","id":"hib1.end","kind":"datetime","label":"End","max":253402300799,"min":-6857222400,"tooltip":"Stop hibernating at this date/time (UTC, whole hours). The tag checks once an hour, on the hour, whether to stop hibernating.","value":0}],"name":"IntervalSchedule","title":"Logging Schedule"},{"controls":[{"choices":[{"id":1,"text":"Activity Bit Per Second","tooltip":""},{"id":2,"text":"Activity Count Per Minute","tooltip":""},{"id":3,"text":"Activity Count Per Four Minutes","tooltip":""},{"id":4,"text":"Activity Count Per Five Minutes","tooltip":""}],"enabled":true,"group":"BitTag Internal Log","id":"bittag_log","kind":"radio","label":"","tooltip":"Set format for BitTag log","value":4}],"name":"BitTagLogFormat","title":"BitTag Internal Log"},{"controls":[{"choices":[{"id":1,"text":"+/- 2g","tooltip":""},{"id":2,"text":"+/- 4g","tooltip":""},{"id":3,"text":"+/- 8g","tooltip":""}],"enabled":true,"group":"Range","id":"adxl.range","kind":"radio","label":"","tooltip":"Set Accelerometer Range","value":2},{"choices":[{"id":1,"text":"12.5 Hz","tooltip":"12.5 samples/second"},{"id":2,"text":"25 Hz","tooltip":"25 samples/second"},{"id":3,"text":"50 Hz","tooltip":"50 samples/second"},{"id":4,"text":"100 Hz","tooltip":"100 samples/second"},{"id":5,"text":"200 Hz","tooltip":"200 samples/second"},{"id":6,"text":"400 Hz","tooltip":"400 samples/second"}],"enabled":true,"group":"Sample Rate","id":"adxl.freq","kind":"radio","label":"","tooltip":"Set Accelerometer Sample Rate","value":3},{"choices":[{"id":2,"text":"1/4 Sample Rate","tooltip":"filter cutoff -- 1/4 sample rate"},{"id":1,"text":"1/2 Sample Rate","tooltip":"filter cutoff -- 1/2 sample rate"}],"enabled":true,"group":"Filter","id":"adxl.filter","kind":"radio","label":"","tooltip":"Set Accelerometer Anti-Alias Filter","value":2},{"decimals":2,"enabled":true,"group":"Activity Detection","id":"adxl.act","kind":"number","label":"Active Threshold","max":2.0,"min":0.03,"step":0.03125,"suffix":"g","tooltip":"Wakeup threshold","value":0.5},{"decimals":2,"enabled":true,"group":"Activity Detection","id":"adxl.inact","kind":"number","label":"Inactive Threshold","max":2.0,"min":0.03,"step":0.03125,"suffix":"g","tooltip":"Sleep threshold","value":0.5},{"decimals":2,"enabled":true,"group":"Activity Detection","id":"adxl.inactive","kind":"number","label":"Inactivity","max":5.1,"min":0.02,"step":0.02,"suffix":"sec","tooltip":"Time below wakeup threshold to become inactive","value":0.5}],"name":"Adxl362Standard","title":"Accelerometer"}];

  /**
   * Creates the mock.
   * @param {URLSearchParams} params The page's query parameters.
   * @returns {{command: Function, query: Function, on: Function}}
   */
  function create(params) {
    const listeners = {};
    let nextId = 1;
    // The simulated tag and document.
    const sim = { form: clone(FORM), attached: true, state: "IDLE", document: true, modified: false, approved: false,
      erase: null, download: null, prompt: null, test: "ALL_PASSED" };
    let timer = null;

    const emit = (event, payload) => (listeners[event] || []).forEach((cb) => cb(clone(payload)));
    const log = (level, text) => emit("log", { level, text });
    const ask = (p) => { sim.prompt = Object.assign({ id: nextId++ }, p); };

    /** Availability, as the core's rule gives it for the simulated tag. */
    function actions() {
      const idle = sim.attached && sim.state === "IDLE";
      const on = {
        attach: !sim.attached, detach: sim.attached, read: idle, sync: idle, test: idle,
        start: idle && sim.document, stop: sim.attached && ["RUNNING", "CONFIGURED", "HIBERNATING"].includes(sim.state),
        erase: sim.attached && ["FINISHED", "ABORTED"].includes(sim.state),
        download: sim.attached && ["FINISHED", "ABORTED"].includes(sim.state) && !sim.download,
        open: true, save: sim.document, reset: sim.document,
      };
      return Object.fromEntries(COMMANDS.map((c) => [c, { visible: true, enabled: on[c] }]));
    }

    /** The view state of the simulation. */
    function view() {
      const a = sim.attached;
      const item = (text, warn) => ({ text, warn: !!warn });
      const name = sim.document ? "BITTAG defaults" : "";
      return {
        title: "Tag Monitor v2.1" + (name ? " — " + name : ""),
        modified: sim.modified,
        attaching: false,
        strip: {
          attached: a, type: a ? "COMPASSTAG" : "", typeTooltip: "Attached tag type; board UUID " + INFO.uuid,
          state: a ? item(sim.state) : item(""), clock: a ? item("+0.00 s") : item(""),
          voltage: a ? "2.84 V" : "", test: a ? item(sim.test, sim.test !== "ALL_PASSED" && sim.test !== "Running") : item(""),
          approved: sim.approved, approvalEnabled: sim.document,
        },
        tag: { enabled: a, info: INFO, internalCount: a ? "128" : "", externalCount: a ? "512" : null,
          downloadEnabled: actions().download.enabled },
        config: { document: sim.document, name, tagType: "BITTAG", modified: sim.modified,
          panels: sim.document ? sim.form : [] },
        actions: actions(),
        recent: [{ path: "/Users/me/configs/compass-1min.json", name: "compass-1min.json" }],
        erase: sim.erase, download: sim.download, prompt: sim.prompt,
      };
    }
    const publish = () => emit("state", view());

    /** Runs a progress (erase or download) to its end. */
    function progress(key, max, done) {
      clearInterval(timer);
      sim[key] = { value: 0, max, text: key === "erase" ? "Erasing flash..." : null };
      timer = setInterval(() => {
        if (!sim[key]) return clearInterval(timer);
        sim[key].value += Math.ceil(max / 12);
        if (sim[key].value >= max) { clearInterval(timer); sim[key] = null; done(); }
        publish();
      }, 250);
    }

    /** Applies a scenario by name. */
    function scenario(name) {
      clearInterval(timer);
      Object.assign(sim, { attached: true, state: "IDLE", document: true, modified: false, approved: false,
        erase: null, download: null, prompt: null, test: "ALL_PASSED" });
      if (name === "detached") Object.assign(sim, { attached: false, document: false });
      if (name === "running") sim.state = "RUNNING";
      if (name === "finished") sim.state = "FINISHED";
      if (name === "erasing") { sim.state = "S_RESET"; progress("erase", 256, () => { sim.state = "IDLE"; }); }
      if (name === "downloading") { sim.state = "FINISHED"; progress("download", 512, () => {}); }
      if (name === "prompt-start") ask({ kind: "startAnyway", title: "Start Tag", text: "Start the tag anyway?",
        failures: ["Clock is -1.45e+09 s off (limit 2 s).", "Configuration is not approved."] });
      if (name === "prompt-unsaved") {
        sim.modified = true;
        ask({ kind: "unsaved", title: "Unsaved Configuration",
          text: "The configuration \"BITTAG defaults\" has unsaved changes.", informative: "Save them before continuing?" });
      }
      if (name === "prompt-base") {
        sim.attached = false;
        ask({ kind: "chooseBase", title: "Available Bases", label: "Please select a base",
          items: ["020:004    0x483:0x3748", "020:007    0x483:0x374b"] });
      }
      if (name === "alert") ask({ kind: "alert", title: "Warning", text: "No Tag Bases Found", informative: "", detail: "", style: "warning" });
      publish();
    }

    /** One command, simulated. */
    function command(cmd, args) {
      switch (cmd) {
        case "ready": scenario(params.get("scenario") || "idle"); return null;
        case "attach": Object.assign(sim, { attached: true, state: "IDLE", document: true }); break;
        case "detach": sim.attached = false; break;
        case "start":
          if (!sim.approved) ask({ kind: "startAnyway", title: "Start Tag", text: "Start the tag anyway?", failures: ["Configuration is not approved."] });
          else sim.state = "RUNNING";
          break;
        case "stop": sim.state = "FINISHED"; break;
        case "test": sim.test = "Running"; setTimeout(() => { sim.test = "ALL_PASSED"; log("INFO", "Self-test on COMPASSTAG finished: ALL_PASSED"); publish(); }, 1200); break;
        case "erase": ask({ kind: "confirmErase", title: "Reset Tag", text: "Erase tag state and data ?" }); break;
        case "download": ask({ kind: "saveFile", title: "Save File", suggested: "/Users/me/untitled.db3", filters: [{ name: "SQLite database", extensions: ["db3"] }] }); break;
        case "open": ask({ kind: "openFile", title: "Open Configuration", start: "/Users/me", filters: [{ name: "Configuration", extensions: ["json"] }] }); break;
        case "save": ask({ kind: "saveFile", title: "Save Configuration", suggested: "/Users/me/compasstag-config.json", filters: [{ name: "Configuration", extensions: ["json"] }] }); break;
        case "reset": sim.modified = true; sim.approved = false; break;
        case "new_config": Object.assign(sim, { document: true, modified: false, approved: false }); break;
        case "set_approved": sim.approved = !!args.on; break;
        case "set_field":
          // No rules here: the value is kept as sent.
          for (const p of sim.form) for (const k of p.controls) if (k.id === args.id) k.value = args.value;
          sim.modified = true;
          sim.approved = false;
          break;
        case "hide_erase": sim.erase = null; break;
        case "cancel_download": sim.download = null; log("INFO", "Downloaded 96 blocks"); break;
        case "quit": log("INFO", "Quit (the app would close)"); break;
        case "answer": {
          const p = sim.prompt;
          if (!p || p.id !== args.id) break;
          sim.prompt = null;
          if (p.kind === "startAnyway" && args.button === "start") sim.state = "RUNNING";
          if (p.kind === "confirmErase" && args.button === "ok") { sim.state = "S_RESET"; progress("erase", 256, () => { sim.state = "IDLE"; }); }
          if (p.kind === "saveFile" && p.title === "Save File" && args.button === "ok") progress("download", 512, () => log("INFO", "Downloaded 512 blocks"));
          if (p.kind === "saveFile" && p.title === "Save Configuration" && args.button === "ok") sim.modified = false;
          if (p.kind === "chooseBase" && args.button === "ok") sim.attached = true;
          break;
        }
        default: break;
      }
      publish();
      return null;
    }

    if (!params.has("nopanel")) {
      const names = ["idle", "detached", "running", "finished", "erasing", "downloading", "prompt-start", "prompt-unsaved", "prompt-base", "alert"];
      const select = Kit.el("select", { class: "kit-select", onchange: (e) => scenario(e.target.value) },
        names.map((n) => Kit.el("option", { value: n }, n)));
      select.value = params.get("scenario") || "idle";
      document.body.append(Kit.el("div", { class: "mock-panel" }, "Scenario", select));
    }

    return {
      command: (cmd, args) => Promise.resolve(command(cmd, args || {})),
      query: (name) => Promise.resolve(clone({
        menus: MENUS,
        about: { title: "About Tag Monitor", text: "Tag Monitor v2.1\n\nAttach to a tag through its USB base, check it, configure and start it, and download its data." },
        user_guide_url: "https://tag-designs.github.io/software/user/apps/qtmonitor.html",
      }[name] ?? null)),
      on: (event, cb) => { (listeners[event] = listeners[event] || []).push(cb); return Promise.resolve(); },
    };
  }

  window.MockEngine = { create };
})();
