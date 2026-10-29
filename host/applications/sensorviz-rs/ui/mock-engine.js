/**
 * @file
 * A stand-in engine for the sensorviz plot spike: the fixture logs (exported
 * by tools/export_mock_data.py into mock/data-*.js) and a synthetic
 * five-million-sample log, served the way the engine will serve them.
 *
 *  - The view state has the engine's shape (core/src/engine.rs): `hasLog`,
 *    `status`, `fileInfo`, `timeDomain`, `view` (streams and choices),
 *    `parameters`, and a `spec` shaped as sensorviz_core's PlotSpec JSON:
 *    traces (id, name, colour, pen width, axis index), axes (side, label,
 *    colour, mode, range), the time domain and UTC offset, the full data
 *    range, event markers, metadata rows and the title.
 *  - Query `envelope {t0, t1, width}` answers with each visible trace's M4
 *    envelope of [t0, t1] at `width` columns, as sensordata::Log::view()
 *    does: per column the first, minimum, maximum and last sample in time
 *    order, plus the samples either side of the window, and a NaN between the
 *    two sides of every gap (a step over four times the median) when the
 *    spec breaks lines at gaps. It also reports how long that took.
 *  - Query `hover {t}` answers with the time and each visible trace's value
 *    at or after t, as text.
 *
 * Derived streams are computed here the way the transforms will compute them
 * (vector magnitudes from the components, altitude from pressure), only so
 * the spike plots what sensorviz plots. ?log=imutag (default), compasstag or
 * big picks the log; a switcher in the corner changes it unless ?nopanel.
 * Defines one global, `MockEngine`.
 */
(function () {
  "use strict";

  /** Decodes base64 into a typed array of the given constructor. */
  function decode(b64, Type) {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    return new Type(bytes.buffer);
  }

  /** Median step of a time array, from at most 65,536 evenly spaced steps. */
  function medianStep(t) {
    const n = t.length;
    if (n < 2) return 0;
    const stride = Math.max(1, Math.floor((n - 1) / 65536));
    const steps = [];
    for (let i = 1; i < n; i += stride) steps.push(t[i] - t[i - 1]);
    steps.sort((a, b) => a - b);
    return steps[steps.length >> 1];
  }

  /** First index with t[i] >= x (binary search). */
  function lowerBound(t, x) {
    let lo = 0, hi = t.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (t[mid] < x) lo = mid + 1; else hi = mid;
    }
    return lo;
  }

  /**
   * A stream: times (Float64Array, seconds), values (Float32Array), its gap
   * threshold, and the metadata the spec needs.
   */
  function stream(meta, t, v) {
    // The metadata first: an exported stream's own `t` and `v` are its base64.
    const gap = 4 * medianStep(t);
    // Every gap, as the index of the sample after it, found once as
    // sensordata does at load.
    const gaps = [];
    for (let i = 1; i < t.length; i++) if (t[i] - t[i - 1] > gap) gaps.push(i);
    return Object.assign({}, meta, { t, v, gap, gaps });
  }

  /** Whether a gap lies between samples a < b: a gap index in (a, b]. */
  function gapBetween(s, a, b) {
    let lo = 0, hi = s.gaps.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (s.gaps[mid] <= a) lo = mid + 1; else hi = mid;
    }
    return lo < s.gaps.length && s.gaps[lo] <= b;
  }

  /** A stream computed sample by sample from others on the same time axis. */
  function computed(meta, inputs, fn) {
    const n = inputs[0].v.length;
    const v = new Float32Array(n);
    const args = new Array(inputs.length);
    for (let i = 0; i < n; i++) {
      for (let k = 0; k < inputs.length; k++) args[k] = inputs[k].v[i];
      v[i] = fn(args);
    }
    return stream(meta, inputs[0].t, v);
  }

  /**
   * The M4 envelope of one stream over [t0, t1] at `width` columns, with the
   * neighbours either side and NaN gap markers.
   * @returns {{t: number[], v: number[]}}
   */
  function envelope(s, t0, t1, width, breakAtGaps) {
    const t = s.t, v = s.v, n = t.length;
    const out = { t: [], v: [] };
    if (!n) return out;
    const i0 = Math.max(0, lowerBound(t, t0) - 1);
    const i1 = Math.min(n - 1, lowerBound(t, t1));
    const span = (t1 - t0) / width;
    let last = -1;
    const push = (i) => {
      if (i === last) return;
      if (breakAtGaps && last >= 0 && gapBetween(s, last, i)) {
        out.t.push((t[i] + t[last]) / 2);
        out.v.push(NaN);
      }
      out.t.push(t[i]);
      out.v.push(v[i]);
      last = i;
    };
    let i = i0;
    while (i <= i1) {
      const col = Math.floor((t[i] - t0) / span);
      let first = i, lo = i, hi = i, end = i;
      while (end + 1 <= i1 && Math.floor((t[end + 1] - t0) / span) === col) {
        end++;
        if (v[end] < v[lo]) lo = end;
        if (v[end] > v[hi]) hi = end;
      }
      const picks = [first, lo, hi, end].sort((a, b) => a - b);
      for (const p of picks) push(p);
      i = end + 1;
    }
    return out;
  }

  /** The value at or after `time`, or null outside the stream or in a gap. */
  function valueAt(s, time) {
    const i = lowerBound(s.t, time);
    if (i >= s.t.length) return null;
    if (i > 0 && s.t[i] - s.t[i - 1] > s.gap) return null;
    return s.v[i];
  }

  // ---- The logs ---------------------------------------------------------

  /** Pressure (mbar) to altitude (m) at the standard sea-level pressure. */
  const altitude = ([p]) => 44330 * (1 - Math.pow(p / 1013.25, 1 / 5.255));
  const magnitude = ([x, y, z]) => Math.sqrt(x * x + y * y + z * z);

  /** The IMUTag fixture, with the magnitudes and altitude sensorviz shows. */
  function imutag() {
    const d = window.SVZ_MOCK_DATA && window.SVZ_MOCK_DATA.imutag;
    if (!d) return null;
    const raw = {};
    for (const s of d.streams) raw[s.id] = stream(s, decode(s.t, Float64Array), decode(s.v, Float32Array));
    const shown = [
      computed({ id: "imu_gyro_magnitude", label: "Gyro magnitude", units: "dps", group: "imu_gyro" },
        [raw.imu_gx, raw.imu_gy, raw.imu_gz], magnitude),
      computed({ id: "imu_accel_magnitude", label: "Acceleration magnitude", units: "mg", group: "imu_accel" },
        [raw.imu_ax, raw.imu_ay, raw.imu_az], magnitude),
      computed({ id: "imu_mag_magnitude", label: "Magnetic field magnitude", units: "uT", group: "imu_mag" },
        [raw.imu_mx, raw.imu_my, raw.imu_mz], magnitude),
      computed({ id: "altitude", label: "Altitude", units: "m", group: "" }, [raw.imu_pressure], altitude),
    ];
    const colors = ["#a0522d", "#2b3a67", "#556b2f", "#7b4fc9"];
    const start = d.start ? new Date(d.start * 1000).toISOString().replace("T", " ").slice(0, 19) + " UTC" : "";
    return {
      name: d.name, domain: "elapsed", info: d.info, streams: shown,
      traces: shown.map((s, i) => ({ id: s.id, color: colors[i], width: s.id === "altitude" ? 1 : 1, side: "left" })),
      markers: [],
      metadata: [["Start:", start], ["MSL:", "1013.25 mbar"]],
    };
  }

  /** The CompassTag fixture: activity on the left, voltage on the right. */
  function compasstag() {
    const d = window.SVZ_MOCK_DATA && window.SVZ_MOCK_DATA.compasstag;
    if (!d) return null;
    const by = {};
    for (const s of d.streams) by[s.id] = stream(s, decode(s.t, Float64Array), decode(s.v, Float32Array));
    const shown = [by.activity, by.core_temperature, by.voltage].filter(Boolean);
    const colors = { activity: "#2f6fdb", core_temperature: "#c0392b", voltage: "#8e44ad" };
    return {
      name: d.name, domain: "epoch", info: d.info, streams: shown,
      traces: shown.map((s) => ({ id: s.id, color: colors[s.id], width: 1, side: s.id === "activity" ? "left" : "right" })),
      markers: [],
      metadata: [["Decl:", "0.00 deg"]],
    };
  }

  /** A synthetic elapsed-time log: 5,000,000 samples at 1 kHz, with a gap and events. */
  function big() {
    const n = 5_000_000;
    const t = new Float64Array(n), v = new Float32Array(n);
    let seed = 1;
    const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647) - 0.5;
    for (let i = 0; i < n; i++) {
      // A 100 s gap after 2,000 s.
      const ti = i / 1000 + (i >= 2_000_000 ? 100 : 0);
      t[i] = ti;
      v[i] = 1000 + 300 * Math.sin(ti / 40) + 120 * Math.sin(ti * 3) + 60 * rand() + (i % 250000 === 0 ? 900 : 0);
    }
    const s = stream({ id: "synthetic", label: "Synthetic signal", units: "mg", group: "" }, t, v);
    return {
      name: "synthetic 5M", domain: "elapsed", info: [["samples", String(n)]], streams: [s],
      traces: [{ id: "synthetic", color: "#2b3a67", width: 1, side: "left" }],
      markers: [500, 1500, 3000, 4500],
      metadata: [["Samples:", "5,000,000"], ["Gap:", "2000-2100 s"]],
    };
  }

  /** The spec for a log: one axis per trace, ranges from the full data plus 5%. */
  function specFor(log, utcOffset, breakAtGaps) {
    let tmin = Infinity, tmax = -Infinity;
    let axes_index = 0;
    const axes = log.traces.map((tr) => {
      const s = log.streams.find((x) => x.id === tr.id);
      let lo = Infinity, hi = -Infinity;
      for (let i = 0; i < s.v.length; i++) {
        const x = s.v[i];
        if (x < lo) lo = x;
        if (x > hi) hi = x;
      }
      tmin = Math.min(tmin, s.t[0]);
      tmax = Math.max(tmax, s.t[s.t.length - 1]);
      const pad = (hi - lo) * 0.05 || 1;
      return { side: tr.side, label: `${s.label} ${s.units}`, color: tr.color, mode: "fixed",
        linkedGroup: "", traces: [axes_index++], range: [lo - pad, hi + pad] };
    });
    return {
      timeDomain: log.domain, utcOffsetHours: utcOffset, breakAtGaps,
      xDataRange: [tmin, tmax],
      traces: log.traces.map((tr, i) => {
        const s = log.streams.find((x) => x.id === tr.id);
        return { id: tr.id, name: `${s.label} ${s.units}`, stream: i, color: tr.color, penWidth: tr.width, axis: i };
      }),
      axes,
      eventMarkerTimes: log.markers,
      metadataRows: log.metadata.map(([k, v]) => `${k.padEnd(8)}${v}`),
      title: log.name,
    };
  }

  /** Hover time text, as sensorviz formats it. */
  function timeText(domain, time, utcOffset) {
    if (domain === "elapsed") return `${time.toFixed(3)} s`;
    const d = new Date((time + utcOffset * 3600) * 1000).toISOString();
    const zone = utcOffset ? `UTC${utcOffset > 0 ? "+" : ""}${utcOffset}` : "UTC";
    return `${d.slice(5, 7)}/${d.slice(8, 10)}/${d.slice(2, 4)} ${d.slice(11, 19)} ${zone}`;
  }

  /**
   * Creates the mock.
   * @param {URLSearchParams} params The page's query parameters.
   * @returns {{command: Function, query: Function, on: Function}}
   */
  function create(params) {
    const listeners = {};
    const emit = (event, payload) => (listeners[event] || []).forEach((cb) => cb(payload));
    const sim = { logName: params.get("log") || "imutag", log: null, utcOffset: 0, breakAtGaps: true, status: "" };
    const loaders = { imutag, compasstag, big };

    function load(name) {
      const t0 = performance.now();
      sim.logName = name;
      sim.log = loaders[name] ? loaders[name]() : null;
      const n = sim.log ? sim.log.streams.reduce((a, s) => a + s.v.length, 0) : 0;
      sim.status = sim.log ? `${n.toLocaleString()} samples loaded in ${(performance.now() - t0).toFixed(0)} ms`
        : `No data for "${name}": run tools/export_mock_data.py (see README)`;
    }

    function view() {
      const log = sim.log;
      return {
        title: "sensorViz",
        hasLog: !!log,
        fileName: log ? log.name : null,
        status: sim.status,
        fileInfo: log ? [log.name].concat(log.info.map(([k, v]) => `${k}: ${v}`)).join("\n") : "",
        timeDomain: log ? log.domain : null,
        hasCompass: false,
        spec: log ? specFor(log, sim.utcOffset, sim.breakAtGaps) : null,
        view: log ? {
          // Every stream, as the engine lists them; those drawn are visible.
          streams: log.streams.map((s) => {
            const tr = log.traces.find((x) => x.id === s.id);
            const color = tr ? tr.color : "#50648c", side = tr ? tr.side : "left";
            return { id: s.id, label: s.label, units: s.units, color, effectiveColor: color, derived: false,
              visible: !!tr, axisSide: side, effectiveAxisSide: side };
          }),
          visibleStreamIds: log.traces.map((tr) => tr.id), customRanges: {},
          title: log.name, titleVisible: true, utcOffsetHours: sim.utcOffset, cursorsVisible: true,
          breakAtGaps: sim.breakAtGaps, parameters: {},
        } : null,
        parameters: [],
        message: null,
      };
    }
    const publish = () => emit("state", view());

    function command(cmd, args) {
      if (cmd === "ready") load(sim.logName);
      else if (cmd === "load") load(args.log);
      else if (cmd === "set_utc_offset") sim.utcOffset = Number(args.hours) || 0;
      else if (cmd === "set_break_at_gaps") sim.breakAtGaps = !!args.on;
      publish();
      return null;
    }

    function query(name, args) {
      const log = sim.log;
      if (name === "envelope") {
        if (!log) return { traces: {}, ms: 0 };
        const t0 = performance.now();
        const traces = {};
        for (const tr of log.traces) {
          const s = log.streams.find((x) => x.id === tr.id);
          traces[tr.id] = envelope(s, args.t0, args.t1, Math.max(1, Math.round(args.width)), sim.breakAtGaps);
        }
        return { traces, ms: performance.now() - t0 };
      }
      if (name === "hover") {
        if (!log) return null;
        const rows = log.traces.map((tr) => {
          const s = log.streams.find((x) => x.id === tr.id);
          const value = valueAt(s, args.t);
          return value === null ? null : { color: tr.color, text: `${s.label}: ${value.toFixed(2)} ${s.units}` };
        }).filter(Boolean);
        return { time: timeText(log.domain, args.t, sim.utcOffset), rows };
      }
      if (name === "range") return { lower: 0, upper: 1 };
      if (name === "about") return { title: "sensorViz", version: "mock", userGuide: null };
      return null;
    }

    if (!params.has("nopanel")) {
      const select = Kit.el("select", { class: "kit-select", onchange: (e) => { load(e.target.value); publish(); } },
        ["imutag", "compasstag", "big"].map((n) => Kit.el("option", { value: n }, n)));
      select.value = sim.logName;
      document.body.append(Kit.el("div", { class: "mock-panel" }, "Log", select));
    }

    return {
      command: (cmd, args) => Promise.resolve(command(cmd, args || {})),
      query: (name, args) => Promise.resolve(query(name, args || {})),
      on: (event, cb) => { (listeners[event] = listeners[event] || []).push(cb); return Promise.resolve(); },
    };
  }

  window.MockEngine = { create };
})();
