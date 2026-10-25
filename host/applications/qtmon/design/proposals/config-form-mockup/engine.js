// A stand-in for the C++ engine's configuration service, in JavaScript only
// so the mockup runs in a browser. In the real design this is Qt-free C++
// beside ConfigShape, and the UI reaches it through the sidecar's messages.
//
// It owns everything that is a rule rather than presentation:
//   - the document: the Config, its source, baseline and unsaved changes;
//   - load/store bindings between Config and form values (each ported from
//     the qtmonitor panel noted beside it);
//   - normalisation: whole UTC hours, rounding direction, clamping to ranges;
//   - constraints that depend on other fields (thresholds follow the range);
//   - whether editing is allowed, and the checks before Start.
//
// Commands in, events out: handle({id, cmd: {case, value}}) returns a list of
// {replyTo, ev: {case, value}}, as the sidecar's protobuf envelope would.

const INT32_MAX = 2147483647;
const HOUR = 3600;
const floorHour = (t) => Math.floor(t / HOUR) * HOUR;
const ceilHour = (t) => Math.ceil(t / HOUR) * HOUR;
const nowS = () => Math.floor(Date.now() / 1000);
const clone = (x) => JSON.parse(JSON.stringify(x));
const ADXL_G = { R2G: 1, R4G: 2, R8G: 4 };              // Adxl362Panel::on_adxlrange_clicked
const ADXL_HZ = { S12_5: 12.5, S25: 25, S50: 50, S100: 100, S200: 200, S400: 400 };

function mergeDeep(base, over) {                         // ConfigShape::merge, roughly
  if (Array.isArray(over) || typeof over !== "object" || over === null) return clone(over);
  const out = clone(base || {});
  for (const k of Object.keys(over)) out[k] = mergeDeep(out[k], over[k]);
  return out;
}

class MockEngine {
  constructor() {
    this.tag = { attached: true, state: "IDLE", clockOffsetS: 0.4, selfTest: "ALL_PASSED", approved: false };
    this.load(clone(window.DEFAULT_CONFIGS.BITTAG_LE), "Defaults", null);
  }

  // ---- commands -------------------------------------------------------------
  handle({ id, cmd }) {
    const reply = (c, v) => ({ replyTo: id, ev: { case: c, value: v } });
    try {
      switch (cmd.case) {
        case "loadDefaults":
          this.load(clone(window.DEFAULT_CONFIGS[cmd.value.tagType]), "Defaults", null);
          return [reply("formSpec", this.spec()), reply("formState", this.state())];
        case "openConfig": {
          const file = JSON.parse(cmd.value.text);
          const type = file.tag_type || cmd.value.fallbackType;
          if (!window.DEFAULT_CONFIGS[type]) return [reply("error", { message: `Unknown tag type ${type}` })];
          // ConfigShape::forFile + merge: the type's defaults with the file on top.
          this.load(mergeDeep(window.DEFAULT_CONFIGS[type], file), "File", cmd.value.path);
          return [reply("formSpec", this.spec()), reply("formState", this.state())];
        }
        case "readFromTag":
          this.load(clone(this.tagConfig || window.DEFAULT_CONFIGS[this.doc.tag_type]), "Tag", null);
          return [reply("formSpec", this.spec()), reply("formState", this.state())];
        case "setField":
          this.setField(cmd.value.panel, cmd.value.field, cmd.value.value);
          return [reply("formState", this.state())];
        case "saveConfig":
          this.baseline = JSON.stringify(this.config());
          this.source = "File"; this.path = cmd.value.path;
          return [reply("saved", { path: cmd.value.path, text: JSON.stringify(this.config(), null, 1) }),
                  reply("formState", this.state())];
        case "setTagStatus":                              // the mockup's stand-in for the live tag
          Object.assign(this.tag, cmd.value);
          return [reply("formState", this.state())];
        case "start": {
          const failures = this.startChecks();
          if (failures.length && !cmd.value.anyway) return [reply("startChecks", { failures })];
          this.tagConfig = this.config(); this.tag.state = "RUNNING";
          return [reply("started", { config: this.config() }), reply("formState", this.state())];
        }
        default:
          return [reply("error", { message: `unknown command ${cmd.case}` })];
      }
    } catch (e) {
      return [reply("error", { message: String(e.message || e) })];
    }
  }

  // ---- the document ------------------------------------------------------------
  load(config, source, path) {
    this.doc = config; this.source = source; this.path = path;
    this.panels = window.FORM_SPEC.panelsForTag[config.tag_type] || [];
    this.values = {}; this.errors = {};
    for (const p of this.panels) Object.assign(this.values, this.loadPanel(p, config));
    this.baseline = JSON.stringify(this.config());      // ConfigTab baseline_
  }

  config() {                                             // ConfigTab::GetConfig: doc + every panel's store()
    const out = clone(this.doc);
    for (const p of this.panels) this.storePanel(p, out);
    return out;
  }

  editable() {                                           // editing is off while the tag's own config runs
    return !(this.source === "Tag" && this.tag.state !== "IDLE");
  }

  spec() {
    return { tagType: this.doc.tag_type,
             panels: this.panels.map((id) => ({ id, ...window.FORM_SPEC.panels[id] })) };
  }

  state() {
    const cfg = this.config();
    return { tagType: this.doc.tag_type, source: this.source, path: this.path,
             modified: JSON.stringify(cfg) !== this.baseline, editable: this.editable(),
             values: clone(this.values), constraints: this.constraints(), errors: clone(this.errors),
             config: cfg, tag: clone(this.tag) };
  }

  startChecks() {                                        // MainWindow::startCheckFailures
    const f = [];
    if (Math.abs(this.tag.clockOffsetS) > 2) f.push(`Clock is ${this.tag.clockOffsetS} s off (limit 2 s).`);
    if (this.tag.selfTest !== "ALL_PASSED") f.push(`Self-test is ${this.tag.selfTest}.`);
    if (!this.tag.approved) f.push("Configuration is not approved.");
    for (const [k, e] of Object.entries(this.errors)) f.push(`${k}: ${e}`);
    return f;
  }

  // ---- bindings: Config <-> form values ---------------------------------------------
  // Keys are "Panel.field". Each binding mirrors the Qt panel's load()/store().

  loadPanel(panel, c) {
    const v = {}, now = ceilHour(nowS());
    const k = (f) => `${panel}.${f}`;
    if (panel.startsWith("IntervalSchedule")) {           // SchedulePanel::load
      const ai = c.active_interval || { start_epoch: 0, end_epoch: INT32_MAX };
      v[k("start")] = ai.start_epoch * 1000 < Date.now() ? { mode: "now", epoch: now } : { mode: "at", epoch: ai.start_epoch };
      v[k("end")] = ai.end_epoch === INT32_MAX ? { mode: "never", epoch: v[k("start")].epoch } : { mode: "at", epoch: ai.end_epoch };
      v[k("hibernate")] = [0, 1].map((i) => {
        const h = (c.hibernate || [])[i] || { start_epoch: 0, end_epoch: 0 };
        const used = !(h.start_epoch === 0 || h.start_epoch === INT32_MAX);
        return { inUse: used, start: used ? h.start_epoch : 0, end: used ? h.end_epoch : 0 };
      });
      if (panel === "IntervalScheduleWithPeriod") v[k("period")] = c.period || 60;
    }
    if (panel === "StartDelaySchedule") v[k("start_delay")] = c.start_delay > 0 ? c.start_delay - 1 : 0;
    if (panel === "BitTagLogFormat") v[k("bittag_log")] = c.bittag_log;
    if (panel.startsWith("Adxl")) {
      const a = c.adxl362 || {};
      if (panel === "Adxl362Standard") { v[k("range")] = a.range || "R4G"; v[k("freq")] = a.freq || "S50"; v[k("filter")] = a.filter || "AAquarter"; }
      v[k("act_thresh_g")] = a.act_thresh_g ?? 0.5;
      if (panel !== "Adxl367Wake") v[k("inact_thresh_g")] = a.inact_thresh_g ?? 0.35;
      v[k("inactive_sec")] = a.inactive_sec ?? 3;
    }
    if (panel === "Lsm6Imu") { const l = c.lsm6 || {}; v[k("odr")] = l.odr; v[k("accelRng")] = l.accelRng; v[k("gyroRng")] = l.gyroRng; }
    return v;
  }

  storePanel(panel, c) {
    const g = (f) => this.values[`${panel}.${f}`];
    if (panel.startsWith("IntervalSchedule")) {           // SchedulePanel::store
      const s = g("start"), e = g("end");
      c.active_interval = {
        start_epoch: s.mode === "now" || s.epoch * 1000 < Date.now() ? 0 : s.epoch,
        end_epoch: e.mode === "at" && e.epoch < INT32_MAX ? e.epoch : INT32_MAX };
      c.hibernate = g("hibernate").map((h) => ({ start_epoch: h.inUse ? h.start : 0, end_epoch: h.inUse ? h.end : 0 }));
      if (panel === "IntervalScheduleWithPeriod") c.period = g("period");
    }
    if (panel === "StartDelaySchedule") c.start_delay = g("start_delay") + 1;
    if (panel === "BitTagLogFormat") c.bittag_log = g("bittag_log");
    if (panel.startsWith("Adxl")) {
      const a = Object.assign({}, c.adxl362);             // fields no panel owns (accel_type) pass through
      if (panel === "Adxl362Standard") { a.range = g("range"); a.freq = g("freq"); a.filter = g("filter"); }
      a.act_thresh_g = g("act_thresh_g");
      if (panel !== "Adxl367Wake") a.inact_thresh_g = g("inact_thresh_g");
      a.inactive_sec = g("inactive_sec");
      c.adxl362 = a;
    }
    if (panel === "Lsm6Imu") c.lsm6 = { odr: g("odr"), accelRng: g("accelRng"), gyroRng: g("gyroRng") };
  }

  // ---- constraints that depend on the document --------------------------------------
  constraints() {
    const out = {};
    for (const p of this.panels) {
      const k = (f) => `${p}.${f}`;
      if (p === "IntervalScheduleWithPeriod") out[k("period")] = { min: 1, max: 300, step: 10, decimals: 0 };
      if (p === "StartDelaySchedule") out[k("start_delay")] = { min: 0, max: 120, step: 1, decimals: 0 };
      if (p === "Adxl362Standard") {                      // thresholds follow the range, inactivity the rate
        const r = ADXL_G[this.values[k("range")]] || 1, dt = 1 / (ADXL_HZ[this.values[k("freq")]] || 12.5);
        out[k("act_thresh_g")] = out[k("inact_thresh_g")] = { min: r / 64, max: r, step: r / 64, decimals: 2 };
        out[k("inactive_sec")] = { min: dt, max: 255 * dt, step: dt, decimals: 2 };
      }
      if (p === "Adxl362Wake") {                          // firmware fixes 4 g and the wake rate
        out[k("act_thresh_g")] = out[k("inact_thresh_g")] = { min: 2 / 64, max: 2, step: 2 / 64, decimals: 2 };
        out[k("inactive_sec")] = { min: 1, max: 255, step: 1, decimals: 0 };
      }
      if (p === "Adxl367Wake") {
        out[k("act_thresh_g")] = { min: 0.2, max: 0.7, step: 0.05, decimals: 2 };
        out[k("inactive_sec")] = { min: 3, max: 12, step: 1, decimals: 0 };
      }
      if (p.startsWith("IntervalSchedule")) out[k("start")] = { minEpoch: ceilHour(nowS()) };
    }
    return out;
  }

  // ---- edits: normalise, clamp, validate ------------------------------------------------
  setField(panel, field, value) {
    if (!this.editable()) throw new Error("This configuration cannot be edited while the tag is running.");
    const key = `${panel}.${field}`;
    if (!(key in this.values)) throw new Error(`no field ${key}`);
    const kind = window.FORM_SPEC.panels[panel].fields.find((f) => f.id === field).kind;
    if (kind === "whenTime") {
      // Whole UTC hours, rounded up (hourtime.h); a start is never before now.
      let epoch = value.epoch ? ceilHour(value.epoch) : this.values[key].epoch;
      if (field === "start") epoch = Math.max(epoch, ceilHour(nowS()));
      value = { mode: value.mode, epoch };
    } else if (kind === "intervals") {
      value = value.map((h, i) => {
        const before = this.values[key][i];
        if (h.inUse && !before.inUse && !h.start) {      // SchedulePanel::hibernateInUseClicked
          const s = ceilHour(this.values[`${panel}.start`].epoch);
          return { inUse: true, start: s, end: s + HOUR };
        }
        // A hibernation start rounds down and its end up, so it covers what was asked.
        return { inUse: h.inUse, start: h.start ? floorHour(h.start) : 0, end: h.end ? ceilHour(h.end) : 0 };
      });
    } else if (kind === "number") {
      // As QDoubleSpinBox: clamp to the range, round to the shown decimals.
      // The step only sets the arrows' increment; typed values are not snapped.
      const c = this.constraints()[key];
      if (c) value = +Math.min(c.max, Math.max(c.min, value)).toFixed(c.decimals);
    }
    this.values[key] = value;
    // Changing the range or rate re-clamps the fields that depend on it.
    if (panel === "Adxl362Standard" && (field === "range" || field === "freq")) {
      const c = this.constraints();
      for (const f of ["act_thresh_g", "inact_thresh_g", "inactive_sec"]) {
        const ck = `${panel}.${f}`;
        this.values[ck] = Math.min(c[ck].max, Math.max(c[ck].min, this.values[ck]));
      }
    }
    this.validate(panel);
  }

  validate(panel) {
    for (const k of Object.keys(this.errors)) if (k.startsWith(panel + ".")) delete this.errors[k];
    if (!panel.startsWith("IntervalSchedule")) return;
    const s = this.values[`${panel}.start`], e = this.values[`${panel}.end`];
    if (e.mode === "at" && e.epoch <= s.epoch) this.errors[`${panel}.end`] = "ends before it starts";
    this.values[`${panel}.hibernate`].forEach((h, i) => {
      if (h.inUse && h.end <= h.start) this.errors[`${panel}.hibernate`] = `row ${i + 1} ends before it starts`;
    });
  }
}

window.MockEngine = MockEngine;
