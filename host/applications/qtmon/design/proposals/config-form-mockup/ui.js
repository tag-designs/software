// The UI: a generic form renderer. It knows field kinds (enum, number,
// whenTime, intervals), never panels or tags, and holds no rules: every edit
// goes to the engine as setField, and the controls show whatever FormState
// comes back, rounded, clamped and validated there. In a Tauri app this file
// would be TypeScript, and send() would write protobuf frames to the sidecar.

const engine = new window.MockEngine();
const $ = (id) => document.getElementById(id);
const log = [];
let nextId = 1, spec = null, controls = {}, lastState = null;

// ---- the message pipe -------------------------------------------------------------
function send(c, value) {
  const msg = { id: nextId++, cmd: { case: c, value } };
  note("→", msg);
  for (const ev of engine.handle(msg)) { note("←", ev); receive(ev.ev); }
}
function receive({ case: c, value }) {
  if (c === "formSpec") renderSpec(value);
  else if (c === "formState") applyState(value);
  else if (c === "startChecks") startDialog(value.failures);
  else if (c === "started") toast(`Started: ${value.config.tag_type} configuration sent.`);
  else if (c === "saved") download(value.path, value.text);
  else if (c === "error") toast(value.message, true);
}
function note(dir, m) {
  const body = JSON.stringify(m.cmd || m.ev);
  log.unshift(`${dir} ${body.length > 220 ? body.slice(0, 220) + "…" : body}`);
  log.length = Math.min(log.length, 40);
  $("messages").textContent = log.join("\n");
}

// ---- time helpers (UTC, whole hours) ---------------------------------------------------
const toInput = (e) => (e ? new Date(e * 1000).toISOString().slice(0, 16) : "");
const fromInput = (s) => (s ? Math.floor(Date.parse(s + ":00Z") / 1000) : 0);

// ---- building the form from the spec ----------------------------------------------------
function el(tag, attrs = {}, parent, text) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) if (v !== undefined) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}

function renderSpec(s) {
  spec = s; controls = {};
  const form = $("form"); form.textContent = "";
  for (const panel of s.panels) {
    const box = el("fieldset", { class: "panel" }, form);
    el("legend", {}, box, panel.title);
    for (const f of panel.fields) {
      const key = `${panel.id}.${f.id}`;
      const row = el("div", { class: "field", title: f.tooltip || "" }, box);
      el("label", { class: "flabel" }, row, f.label);
      const body = el("div", { class: "fbody" }, row);
      controls[key] = build(panel.id, f, body);
      controls[key].error = el("div", { class: "error" }, row);
    }
  }
}

function build(panel, f, body) {
  const set = (value) => send("setField", { panel, field: f.id, value });
  if (f.kind === "enum") {
    const group = el("div", { class: "seg", role: "radiogroup" }, body);
    const buttons = f.options.map((o) => {
      const b = el("button", { type: "button", role: "radio", title: o.tooltip }, group, o.label);
      b.addEventListener("click", () => set(o.value));
      return [o.value, b];
    });
    return { update(v) { buttons.forEach(([val, b]) => { b.classList.toggle("on", val === v); b.setAttribute("aria-checked", val === v); }); },
             disable(d) { buttons.forEach(([, b]) => (b.disabled = d)); } };
  }
  if (f.kind === "number") {
    const input = el("input", { type: "number" }, body);
    if (f.units) el("span", { class: "units" }, body, f.units);
    input.addEventListener("change", () => set(+input.value));
    return { update(v, c) { if (c) { input.min = c.min; input.max = c.max; input.step = c.step; }
                            input.value = c ? (+v).toFixed(c.decimals) : v; },
             disable(d) { input.disabled = d; } };
  }
  if (f.kind === "whenTime") {
    const radios = f.options.map((o) => {
      const l = el("label", { class: "opt", title: o.tooltip }, body);
      const r = el("input", { type: "radio", name: `${panel}.${f.id}` }, l); l.append(" " + o.label);
      return [o.value, r];
    });
    const time = el("input", { type: "datetime-local", step: 3600, title: f.timeTooltip }, body);
    el("span", { class: "units" }, body, "UTC");
    let current;
    const push = (mode) => set({ mode, epoch: fromInput(time.value) || current.epoch });
    radios.forEach(([val, r]) => r.addEventListener("change", () => push(val)));
    time.addEventListener("change", () => push(current.mode));
    return { update(v, c) { current = v; radios.forEach(([val, r]) => (r.checked = val === v.mode));
                            time.value = toInput(v.epoch); time.disabled = v.mode === f.options[0].value || this.off;
                            if (c && c.minEpoch) time.min = toInput(c.minEpoch); },
             disable(d) { this.off = d; radios.forEach(([, r]) => (r.disabled = d)); } };
  }
  if (f.kind === "intervals") {
    const rows = Array.from({ length: f.rows }, (_, i) => {
      const r = el("div", { class: "hib" }, body);
      const lab = el("label", { class: "opt", title: f.tooltip }, r);
      const use = el("input", { type: "checkbox" }, lab); lab.append(` In use`);
      el("span", {}, r, "Start");
      const s = el("input", { type: "datetime-local", step: 3600, title: f.startTooltip }, r);
      el("span", {}, r, "End");
      const e = el("input", { type: "datetime-local", step: 3600, title: f.endTooltip }, r);
      el("span", { class: "units" }, r, "UTC");
      return { use, s, e };
    });
    let current;
    const push = () => set(rows.map((r, i) => ({ inUse: r.use.checked,
      start: fromInput(r.s.value) || (current[i] && current[i].start) || 0,
      end: fromInput(r.e.value) || (current[i] && current[i].end) || 0 })));
    rows.forEach((r) => [r.use, r.s, r.e].forEach((x) => x.addEventListener("change", push)));
    return { update(v) { current = v; rows.forEach((r, i) => { const h = v[i] || {};
               r.use.checked = !!h.inUse; r.s.value = toInput(h.start); r.e.value = toInput(h.end);
               r.s.disabled = r.e.disabled = !h.inUse || this.off; }); },
             disable(d) { this.off = d; rows.forEach((r) => (r.use.disabled = d)); } };
  }
  el("span", {}, body, `(no renderer for ${f.kind})`);
  return { update() {}, disable() {} };
}

// ---- applying state ----------------------------------------------------------------------
function applyState(s) {
  lastState = s;
  for (const [key, c] of Object.entries(controls)) {
    c.disable(!s.editable);
    c.update(s.values[key], s.constraints[key]);
    c.error.textContent = s.errors[key] || "";
  }
  $("tagType").value = s.tagType;
  $("doc").textContent = `${s.tagType} · ${s.source}${s.path ? " · " + s.path : ""}`;
  $("modified").hidden = !s.modified;
  $("readonly").hidden = s.editable;
  $("config").textContent = JSON.stringify(s.config, null, 1);
  $("tagState").value = s.tag.state; $("clock").value = s.tag.clockOffsetS;
  $("selfTest").value = s.tag.selfTest; $("approved").checked = s.tag.approved;
  document.title = `Tag Monitor configuration${s.modified ? " •" : ""}`;
}

// ---- dialogs and files (presentation only) ---------------------------------------------------
function startDialog(failures) {
  $("failures").textContent = "";
  failures.forEach((f) => el("li", {}, $("failures"), f));
  $("startDialog").showModal();
}
function toast(text, bad) {
  const t = $("toast"); t.textContent = text; t.className = bad ? "toast bad" : "toast"; t.hidden = false;
  clearTimeout(toast.timer); toast.timer = setTimeout(() => (t.hidden = true), 4000);
}
function download(name, text) {
  const a = el("a", { href: URL.createObjectURL(new Blob([text], { type: "application/json" })), download: name });
  a.click(); URL.revokeObjectURL(a.href);
}

$("tagType").addEventListener("change", () => send("loadDefaults", { tagType: $("tagType").value }));
$("defaults").addEventListener("click", () => send("loadDefaults", { tagType: $("tagType").value }));
$("read").addEventListener("click", () => send("readFromTag", {}));
$("save").addEventListener("click", () => send("saveConfig", { path: `${lastState.tagType.toLowerCase()}-config.json` }));
$("openFile").addEventListener("change", async (e) => {
  const file = e.target.files[0]; if (!file) return;
  send("openConfig", { path: file.name, text: await file.text(), fallbackType: $("tagType").value });
  e.target.value = "";
});
$("start").addEventListener("click", () => send("start", { anyway: false }));
$("startAnyway").addEventListener("click", () => { $("startDialog").close(); send("start", { anyway: true }); });
$("cancelStart").addEventListener("click", () => $("startDialog").close());
const tagStatus = () => send("setTagStatus", { state: $("tagState").value, clockOffsetS: +$("clock").value,
                                              selfTest: $("selfTest").value, approved: $("approved").checked });
["tagState", "clock", "selfTest", "approved"].forEach((id) => $(id).addEventListener("change", tagStatus));

Object.keys(window.FORM_SPEC.panelsForTag).forEach((t) => el("option", { value: t }, $("tagType"), t));
const first = new URLSearchParams(location.search).get("tag") || "BITTAG_LE";
send("loadDefaults", { tagType: first });
