/**
 * @file
 * A stand-in engine for designing the page in a plain browser: canned view
 * states in the shape of design/ui-engine-contract.md, and enough behaviour
 * (prompts answered, a fake programming session) to click through. Not used
 * in the app, where Kit.createEngine finds Tauri first.
 *
 * ?scenario= station (default; the qtprogram screenshot), pending, fail,
 * running, session, progress, powercycle, finished, prompt-auto,
 * prompt-record, prompt-base, alert. A switcher in the corner changes it
 * live unless ?nopanel is given.
 *
 * The mock holds no rules: its states and answers are hand-written samples
 * of the contract's shapes, kept in step with it by hand. When the contract
 * changes, update the samples here so the preview still exercises every
 * field the page reads. Defines one global, `MockEngine`.
 */
(function () {
  "use strict";

  /**
   * A deep copy of plain JSON data, so states handed to the page cannot be
   * changed under the mock (and vice versa).
   * @param {*} o
   * @returns {*}
   */
  const clone = (o) => JSON.parse(JSON.stringify(o));

  /** The default scenario: a ready station with a BitTag attached. */
  const STATION = {
    title: "Tag Programmer v2.0",
    firmware: {
      items: [
        { id: "release:fw-v0.7", text: "fw-v0.7  (2026-10-15)" },
        { id: "release:fw-v0.6", text: "fw-v0.6  (2026-09-30)" },
        { id: "release:fw-v0.5", text: "fw-v0.5  (2026-08-12)  not downloaded" },
        { separator: true },
        { id: "file:/Users/me/builds/BitTag.elf", text: "BitTag.elf  (/Users/me/builds)" },
        { separator: true },
        { id: "other", text: "Other ELF file..." },
      ],
      selected: "release:fw-v0.7",
      enabled: true,
      releaseNotesUrl: "https://github.com/tag-designs/software/releases/tag/fw-v0.7",
    },
    tagType: { items: ["BitTag", "CompassTag", "PresTag", "UIUCTag"], selected: "BitTag", enabled: true,
      note: "detected from the tag" },
    checks: [
      { name: "Programmer", level: "ok", text: "probe-rs 0.32.0" },
      { name: "Base", level: "ok", text: "ST-LINK 0037001A5556431520303537: bittag-base-v7 a473918f" },
      { name: "SWD", level: "ok", text: "0x435 STM32L43xxx/STM32L44xxx" },
      { name: "Supply", level: "ok", text: "3.23 V" },
      { name: "Tag", level: "ok", text: "BitTag V6, 169cda2c" },
      { name: "Tag state", level: "ok", text: "IDLE" },
      { name: "Firmware", level: "ok", text: "BitTag (fw-v0.7), verified against its manifest" },
      { name: "Record", level: "ok", text: "programmed-tags.csv (1 row)" },
    ],
    rowButtons: { recordChoose: false, stop: { visible: false, enabled: true }, erase: { visible: false, enabled: true } },
    tag: {
      label: "BitTag-bench (recorded)",
      uuid: "2035374D303150190059002F",
      state: "IDLE",
      firmware: "BitTag  169cda2c  (BitTag V6)",
      tests: "ALL_PASSED",
      clock: { text: "+0.02 s", warn: false },
      syncEnabled: true,
      result: { text: "", ok: null },
    },
    actions: {
      program: { enabled: true, tooltip: "" },
      retest: { enabled: true, tooltip: "" },
      nextTag: { enabled: true, tooltip: "" },
      attach: { enabled: true, tooltip: "" },
    },
    session: { stepText: "", progress: null, retryVisible: false, cancelVisible: false, cancelText: "Cancel" },
    batch: "Batch: 0 programmed this session, 0 not OK",
    autoAttach: true,
    recordPath: "/Users/me/Documents/programmed-tags.csv",
    prompt: null,
  };

  /** Tag panel fields as the engine shows them when no tag is known. */
  const DASH = { uuid: "—", state: "—", firmware: "—", tests: "—", clock: { text: "—", warn: false }, syncEnabled: false };
  /**
   * Every action disabled, with `why` as the tooltip.
   * @param {string} why
   * @returns {Object} An `actions` section.
   */
  const busyActions = (why) => ({
    program: { enabled: false, tooltip: why },
    retest: { enabled: false, tooltip: why },
    nextTag: { enabled: false, tooltip: why },
    attach: { enabled: false, tooltip: why },
  });

  /** Counter giving each canned prompt a fresh id, as the engine does. */
  let promptSeq = 0;
  /**
   * `p` with a new prompt id, so the page shows it even after an earlier
   * prompt of the same kind.
   * @param {Object} p
   * @returns {Object}
   */
  const prompt = (p) => Object.assign({ id: "p" + ++promptSeq }, p);

  /**
   * Builds the named scenario from the station state.
   *
   * An unknown name gives the station state.
   * @param {string} name One of SCENARIOS.
   * @returns {Object} A fresh view state.
   */
  function scenario(name) {
    const s = clone(STATION);
    const check = (n) => s.checks.find((c) => c.name === n);
    switch (name) {
      case "pending":
        for (const c of s.checks.slice(1)) { c.level = "pending"; c.text = ""; }
        check("Base").text = "Looking for a base";
        Object.assign(s.tag, DASH, { label: "" });
        s.tagType.note = "";
        s.actions = busyActions("Waiting for the station checks");
        s.actions.nextTag.enabled = true;
        break;
      case "fail":
        check("Supply").level = "warn"; check("Supply").text = "2.41 V, below 2.5 V: the tag may brown out while programming";
        check("Tag").level = "fail"; check("Tag").text = "No firmware answers (a blank board, or the tag is not seated)";
        check("Tag state").level = "pending"; check("Tag state").text = "";
        check("Firmware").level = "pending"; check("Firmware").text = "";
        check("Record").level = "fail"; check("Record").text = "programmed-tags.csv is not writable: Permission denied";
        s.rowButtons.recordChoose = true;
        Object.assign(s.tag, DASH, { label: "" });
        s.tag.result = { text: "FAILED: the tag does not answer", ok: false };
        s.actions.program = { enabled: false, tooltip: "Choose a record file that can be written" };
        s.actions.retest = { enabled: false, tooltip: "" };
        break;
      case "running":
        check("Tag state").level = "warn"; check("Tag state").text = "RUNNING: a deployment is logging; stop it to program";
        s.tag.state = "RUNNING";
        s.tag.clock = { text: "+4.31 s  (press Sync)", warn: true };
        s.rowButtons.stop.visible = true;
        break;
      case "finished":
        check("Tag state").level = "warn"; check("Tag state").text = "FINISHED: it holds a log";
        s.tag.state = "FINISHED";
        s.rowButtons.erase.visible = true;
        s.tag.label = "BitTag-017 (recorded)";
        s.tag.result = { text: "OK", ok: true };
        s.batch = "Batch: 1 programmed this session, 0 not OK";
        break;
      case "session":
        s.session = { stepText: "Programming BitTag (fw-v0.7): writing flash", progress: "busy",
          retryVisible: false, cancelVisible: false, cancelText: "Cancel" };
        s.actions = busyActions("Programming is in progress");
        s.firmware.enabled = false; s.tagType.enabled = false; s.tag.syncEnabled = false;
        break;
      case "progress":
        check("Tag state").text = "FINISHED";
        s.session = { stepText: "Erasing the tag's log", progress: { done: 17, total: 40 },
          retryVisible: false, cancelVisible: true, cancelText: "Cancel" };
        s.actions = busyActions("The tag is being erased");
        s.rowButtons.erase = { visible: true, enabled: false };
        s.firmware.enabled = false; s.tagType.enabled = false; s.tag.syncEnabled = false;
        break;
      case "powercycle":
        check("Tag").level = "fail"; check("Tag").text = "No firmware answers";
        s.session = { stepText: "Programmed, but the tag does not answer. A mass erase can leave it in its bootloader until it is powered off: unplug the base, plug it in again, and press Retry.",
          progress: null, retryVisible: true, cancelVisible: true, cancelText: "Give Up" };
        s.actions = busyActions("Programming is in progress");
        break;
      case "prompt-auto":
        s.prompt = prompt({ kind: "autoAttach" });
        break;
      case "prompt-record":
        s.prompt = prompt({ kind: "recordFile", defaultPath: "/Users/me/Documents/programmed-tags.csv", exists: false });
        break;
      case "prompt-base":
        s.prompt = prompt({ kind: "chooseBase", label: "Program through which base?", current: 0,
          items: ["ST-LINK 0037001A5556431520303537 (BitTag base)", "ST-LINK 066DFF545150898367194021"] });
        break;
      case "alert":
        s.prompt = prompt({ kind: "alert", title: "Choose a Base", text: "No ST-LINK is on USB, or programming is in progress.", style: "info" });
        break;
    }
    return s;
  }

  /** The scenario names, in the switcher's order. */
  const SCENARIOS = ["station", "pending", "fail", "running", "finished", "session", "progress", "powercycle",
    "prompt-auto", "prompt-record", "prompt-base", "alert"];

  /**
   * Creates a mock engine starting in the scenario `?scenario=` names.
   *
   * `command` handles the commands the page sends (unknown ones are only
   * logged), then publishes the state; `query` answers the page's queries
   * with fixed samples (unknown ones with null); `on` accepts the `state`
   * and `log` events only. The extra `setScenario(name)` switches state.
   * @param {URLSearchParams} params The page's query parameters.
   * @returns {{command: Function, query: Function, on: Function, setScenario: Function}}
   */
  function create(params) {
    const listeners = { state: [], log: [] };
    let s = scenario(params.get("scenario") || "station");
    let timer = null;

    const emit = (event, payload) => listeners[event].forEach((cb) => cb(clone(payload)));
    const publish = () => emit("state", s);
    const log = (level, text) => emit("log", { level, text });

    /**
     * Plays a programming session: 40 progress steps 100 ms apart, then the
     * state from before it with an OK result for `label`.
     * @param {string} label
     */
    function fakeSession(label) {
      let done = 0;
      const steps = ["Stopping the tag", "Erasing flash", "Writing flash", "Connecting to the new image", "Testing"];
      const base = clone(s);
      s.actions = busyActions("Programming is in progress");
      s.firmware.enabled = false;
      clearInterval(timer);
      timer = setInterval(() => {
        done++;
        const step = steps[Math.min(steps.length - 1, Math.floor(done / 8))];
        s.session = { stepText: `Programming ${label}: ${step}`, progress: { done, total: 40 },
          retryVisible: false, cancelVisible: false, cancelText: "Cancel" };
        if (done % 8 === 1) log("INFO", step);
        if (done >= 40) {
          clearInterval(timer);
          s = base;
          s.tag.label = label + " (recorded)";
          s.tag.result = { text: "OK", ok: true };
          s.checks.find((c) => c.name === "Record").text = "programmed-tags.csv (2 rows)";
          s.session.stepText = `Programmed ${label}: OK`;
          s.batch = "Batch: 1 programmed this session, 0 not OK";
          log("INFO", `Programmed ${label}: OK`);
        }
        publish();
      }, 100);
    }

    /** Command name to its effect on the mock state. */
    const handlers = {
      start: () => log("INFO", "Tag Programmer v2.0 (mock engine)"),
      set_auto_attach: (a) => { s.autoAttach = a.on; },
      answer_auto_attach: (a) => { s.autoAttach = a.on; s.prompt = null; },
      record_use_default: () => { s.prompt = null; },
      record_open: () => { s.prompt = null; },
      record_create: () => { s.prompt = null; },
      choose_base: () => { s.prompt = null; },
      prompt_cancel: () => { s.prompt = null; },
      list_bases: () => { s.prompt = scenario("prompt-base").prompt; },
      choose_record: () => { s.prompt = prompt({ kind: "recordFile", defaultPath: s.recordPath, exists: true }); },
      choose_firmware: (a) => { s.firmware.selected = a.id; },
      choose_file: (a) => {
        const dir = a.path.replace(/\/[^/]*$/, "");
        const id = "file:" + a.path;
        if (!s.firmware.items.some((i) => i.id === id)) {
          s.firmware.items.splice(s.firmware.items.findIndex((i) => i.separator) + 1, 0,
            { id, text: `${a.path.slice(dir.length + 1)}  (${dir})` });
        }
        s.firmware.selected = id;
        s.tagType.note = "from the image";
      },
      choose_target: (a) => { s.tagType.selected = a.name; },
      program: (a) => fakeSession(a.label),
      retest: (a) => { s.tag.result = { text: "OK", ok: true }; log("INFO", `Retested ${a.label}: OK`); },
      cancel_session: () => { clearInterval(timer); s = scenario("station"); },
      retry_connect: () => { s = scenario("station"); },
    };

    return {
      command(cmd, args) {
        if (cmd !== "set_log_level") log("DEBUG", `command ${cmd} ${JSON.stringify(args || {})}`);
        if (handlers[cmd]) handlers[cmd](args || {});
        publish();
        return Promise.resolve(null);
      },
      query(name, args) {
        const answers = {
          program_preview: () => ({
            ok: true, reason: "",
            tag: "BitTag  IDLE  UUID 2035374D303150190059002F  3.23 V",
            image: "BitTag (fw-v0.7), commit 169cda2c\nSHA-256 18d6fc13d3711fe4e9b7fa8b7e09be6fa2d0cbce09a544e83c0bbe406bd7a8ac",
            erase: "Mass erase and program (STM32L432 with no calibration page)",
            lost: ["The tag's log. Download it first with Tag Monitor.", "The tag's stored configuration."],
            kept: ["The RTC trim and the board UUID."],
            notes: [],
          }),
          label_conflict: () => (args.label === "BitTag-017" ? "BitTag-017 is already recorded for another tag, UUID 0039002A3233510D33353533." : null),
          label_notes: () => (args.label === "BitTag-bench" ? [] : ["This tag is recorded as BitTag-bench."]),
          retest_default_label: () => "BitTag-bench",
          about: () => ({ title: "About Tag Programmer",
            text: "Tag Programmer v2.0\n\nProgram and test a batch of tags through a USB base, recording each one." }),
          user_guide_url: () => null,
        };
        return Promise.resolve(answers[name] ? answers[name]() : null);
      },
      on(event, cb) {
        listeners[event].push(cb);
        return Promise.resolve(() => {});
      },
      setScenario(name) { clearInterval(timer); s = scenario(name); publish(); },
    };
  }

  /**
   * The scenario switcher shown in a browser preview.
   * @param {Object} engine A mock engine from create().
   * @param {string} current The scenario selected at first.
   */
  function panel(engine, current) {
    const select = Kit.el("select", { class: "kit-select", onchange: (e) => engine.setScenario(e.target.value) },
      SCENARIOS.map((n) => Kit.el("option", { value: n, selected: n === current }, n)));
    document.body.append(Kit.el("div", { class: "mock-panel" }, "Mock state", select));
  }

  /** The global the page passes to Kit.createEngine outside the app. */
  window.MockEngine = {
    create(params) {
      const engine = create(params);
      if (!params.has("nopanel")) panel(engine, params.get("scenario") || "station");
      return engine;
    },
  };
})();
