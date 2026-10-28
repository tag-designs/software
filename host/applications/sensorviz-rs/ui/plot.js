/**
 * @file
 * The sensorviz plot in the page: draws a PlotSpec with uPlot from the
 * engine's M4 envelopes, with sensorviz's interactions. The browser
 * counterpart of the Qt app's PlotView (plot_view.cpp).
 *
 *  - Data: the plot never holds a stream's samples. Whenever the time window
 *    or the width changes it asks the engine (query `envelope`) for each
 *    trace's envelope at one column per device pixel, aligns the traces on
 *    one time array (uPlot's data model) and redraws. A trace's entries at
 *    the other traces' times are filled by linear interpolation between its
 *    own neighbours, so they lie on the line it draws anyway -- uPlot breaks
 *    a line at a missing value -- and a gap marker (NaN) becomes `null`,
 *    which breaks it where the engine said. The y axes keep the spec's
 *    ranges; only time moves.
 *  - Mouse: dragging pans time and the wheel zooms it; double-click places
 *    the left cursor and shift-double-click the right one, showing them if
 *    hidden. The metadata box and the legend can be dragged; their positions
 *    are kept as fractions of the plot area across redraws and resizes.
 *  - Hover: the time and every trace's value under the mouse (query
 *    `hover`), in a tooltip; `onHover(t)` is told the time as well, for the
 *    compass panel.
 *
 * Defines one global, `SvzPlot`. Load after uPlot.iife.min.js.
 */
(function () {
  "use strict";

  /** Two-line epoch tick labels, as sensorviz shows them: HH:MM over MM/DD/YY. */
  function epochTicks(utcOffset) {
    return (u, splits) => splits.map((s) => {
      if (s == null) return "";
      const d = new Date((s + utcOffset * 3600) * 1000).toISOString();
      return `${d.slice(11, 16)}\n${d.slice(5, 7)}/${d.slice(8, 10)}/${d.slice(2, 4)}`;
    });
  }

  /** The IANA zone that shifts UTC by whole hours, for uPlot's tick placement. */
  const etcZone = (hours) => (hours ? `Etc/GMT${hours > 0 ? "-" : "+"}${Math.abs(hours)}` : "Etc/UTC");

  /**
   * Aligns the traces' envelopes on one sorted time array.
   * @param {Object} spec The PlotSpec.
   * @param {Object} env `{id: {t, v}}` from the engine.
   * @returns {Array} uPlot data: [times, values per trace...].
   */
  function align(spec, env) {
    const all = [];
    for (const tr of spec.traces) {
      const e = env[tr.id];
      if (e) for (let i = 0; i < e.t.length; i++) all.push(e.t[i]);
    }
    all.sort((a, b) => a - b);
    const xs = [];
    for (const t of all) if (!xs.length || xs[xs.length - 1] !== t) xs.push(t);
    const data = [xs];
    for (const tr of spec.traces) {
      const e = env[tr.id];
      const ys = new Array(xs.length);   // undefined: no sample here, the line crosses
      if (e) {
        let j = 0;
        let prev = -1;   // the last of this trace's own points, by index in xs
        for (let i = 0; i < e.t.length; i++) {
          while (xs[j] < e.t[i]) j++;
          const v = e.v[i];
          ys[j] = Number.isNaN(v) ? null : v;   // null: a gap, the line breaks
          // Fill the other traces' times since the last point of this one,
          // on the segment between them, unless either end is a gap.
          if (prev >= 0 && ys[prev] !== null && ys[j] !== null && j - prev > 1) {
            const x0 = xs[prev], y0 = ys[prev], k = (ys[j] - y0) / (xs[j] - x0);
            for (let m = prev + 1; m < j; m++) ys[m] = y0 + k * (xs[m] - x0);
          }
          prev = j;
        }
      }
      data.push(ys);
    }
    return data;
  }

  /**
   * Makes a box inside the plot draggable, keeping its position as fractions
   * of the plot area so it stays put across resizes.
   * @returns {{place: Function}} `place()` re-applies the position.
   */
  function draggable(box, area, start) {
    const pos = { fx: start[0], fy: start[1] };
    const place = () => {
      const w = area.clientWidth - box.offsetWidth, h = area.clientHeight - box.offsetHeight;
      box.style.left = `${Math.max(0, Math.min(w, pos.fx * area.clientWidth))}px`;
      box.style.top = `${Math.max(0, Math.min(h, pos.fy * area.clientHeight))}px`;
    };
    box.addEventListener("pointerdown", (ev) => {
      ev.stopPropagation();
      box.setPointerCapture(ev.pointerId);
      const sx = ev.clientX, sy = ev.clientY, ox = box.offsetLeft, oy = box.offsetTop;
      const move = (e) => {
        pos.fx = (ox + e.clientX - sx) / area.clientWidth;
        pos.fy = (oy + e.clientY - sy) / area.clientHeight;
        place();
      };
      const up = () => { box.removeEventListener("pointermove", move); box.removeEventListener("pointerup", up); };
      box.addEventListener("pointermove", move);
      box.addEventListener("pointerup", up);
    });
    return { place, pos };
  }

  /**
   * A plot in `host`.
   * @param {HTMLElement} host Where it draws; it fills it.
   * @param {Object} opts `query(name, args)` -> Promise (the engine's);
   *     `onHover(t|null)`; `onStats({ms, points})` after each refill.
   */
  function SvzPlot(host, opts) {
    this.host = host;
    this.opts = opts;
    this.spec = null;
    this.u = null;
    this.cursors = { left: null, right: null, visible: true };
    this.pending = false;
    this.seq = 0;
    this.lastWindow = "";
    host.classList.add("svz-plot");
    this.titleEl = Kit.el("div", { class: "svz-title" });
    this.chartEl = Kit.el("div", { class: "svz-chart" });
    this.tipEl = Kit.el("div", { class: "svz-tip", hidden: true });
    host.append(this.titleEl, this.chartEl, this.tipEl);
    new ResizeObserver(() => this.resize()).observe(this.chartEl);
  }

  /** Draws a new spec, keeping the time window when the domain is unchanged. */
  SvzPlot.prototype.setSpec = function (spec) {
    const old = this.spec;
    this.spec = spec;
    this.titleEl.textContent = spec ? spec.title : "";
    this.titleEl.hidden = !spec || !spec.title;
    let keep = null;
    if (this.u && old && spec && old.domain === spec.domain) keep = [this.u.scales.x.min, this.u.scales.x.max];
    if (this.u) { this.u.destroy(); this.u = null; }
    this.chartEl.replaceChildren();
    if (!spec) return;
    this.build(spec);
    const [a, b] = spec.xDataRange;
    const pad = (b - a) * 0.05 || 1;
    const x = keep || [a - pad, b + pad];
    this.u.setScale("x", { min: x[0], max: x[1] });
    this.resetCursors(x);
  };

  /**
   * uPlot options for a spec: scales, axes and series.
   * @param {Object} spec The PlotSpec.
   * @param {boolean} print Black on white, for a PDF; otherwise the page's
   *     theme.
   * @param {number} [k] Scale for every size -- fonts, axis widths, ticks,
   *     lines and dashes -- so that a chart drawn k times larger at pixel
   *     ratio 1 looks like the screen's at ratio k. uPlot has one pixel ratio,
   *     the screen's, for every chart, so this is how a PDF is drawn at print
   *     resolution.
   * @returns {Object} Options without size, hooks or cursor.
   */
  function chartOptions(spec, print, k) {
    k = k || 1;
    const css = getComputedStyle(document.documentElement);
    const ink = print ? "#000" : css.getPropertyValue("--text").trim() || "#222";
    const grid = print ? "#ccc" : css.getPropertyValue("--separator").trim() || "#ddd";
    const font = `${12 * k}px ` + (css.getPropertyValue("--font").trim() || "sans-serif");
    const tick = (stroke) => ({ stroke, size: 10 * k, width: k });
    const gridLine = (show) => ({ show, stroke: grid, width: k, dash: [2 * k, 3 * k] });
    // Grid lines come from the innermost left axis -- the spec's last -- as in
    // the Qt plot.
    const innermostLeft = spec.axes.map((a, i) => [a, i]).filter(([a]) => a.side === "left").pop();
    const epoch = spec.domain === "epoch";
    const zone = etcZone(spec.utcOffset);
    const axes = [{
      stroke: ink, font, labelFont: font, gap: 5 * k, labelSize: 30 * k, labelGap: 0,
      space: 50 * k,   // uPlot's minimum spacing between ticks, in pixels
      grid: gridLine(true),
      ticks: tick(ink),
      label: epoch ? `Hour:Minute (${spec.utcOffset ? `UTC${spec.utcOffset > 0 ? "+" : ""}${spec.utcOffset}` : "UTC"})` : "Elapsed Time (s)",
      size: (epoch ? 54 : 40) * k,
      values: epoch ? epochTicks(spec.utcOffset) : undefined,
    }];
    const scales = { x: { time: epoch, auto: false } };
    // uPlot stacks each side's axes outward in the order given; the Qt plot
    // puts the spec's first axis outermost, so they are given last first.
    spec.axes.map((a, i) => [a, i]).reverse().forEach(([a, i]) => {
      scales[`y${i}`] = { auto: false, range: () => a.range };
      axes.push({
        scale: `y${i}`, side: a.side === "left" ? 3 : 1, label: a.label, stroke: a.color, font, labelFont: font,
        gap: 5 * k, labelSize: 30 * k, labelGap: 0, space: 30 * k,
        ticks: tick(a.color), size: 54 * k,
        grid: gridLine(innermostLeft && innermostLeft[1] === i),
      });
    });
    const series = [{}].concat(spec.traces.map((tr) => ({
      label: tr.name, scale: `y${tr.axis}`, stroke: tr.color, width: tr.width * k,
      spanGaps: false, points: { show: false },
    })));
    return {
      scales, axes, series, legend: { show: false },
      tzDate: epoch ? (ts) => uPlot.tzDate(new Date(ts * 1000), zone) : undefined,
    };
  }

  /** Builds the uPlot instance and the overlays for a spec. */
  SvzPlot.prototype.build = function (spec) {
    const self = this;
    const size = this.size();
    this.u = new uPlot(Object.assign(chartOptions(spec, false), {
      width: size.width, height: size.height,
      cursor: { drag: { x: false, y: false, setScale: false }, points: { show: false }, y: false },
      hooks: {
        setScale: [(u, key) => { if (key === "x") self.requestEnvelope(); }],
        setSize: [() => self.requestEnvelope()],
        draw: [(u) => self.drawOverlays(u)],
        setCursor: [(u) => self.hover(u)],
      },
    }), [[], ...spec.traces.map(() => [])], this.chartEl);
    window.__svzPlot = this;   // for the spike's browser checks
    this.installMouse();
    this.installBoxes(spec);
  };

  /** The chart's size in CSS pixels. */
  SvzPlot.prototype.size = function () {
    return { width: Math.max(200, this.chartEl.clientWidth), height: Math.max(150, this.chartEl.clientHeight) };
  };

  /** Follows the host's size. */
  SvzPlot.prototype.resize = function () {
    if (!this.u) return;
    const s = this.size();
    if (s.width !== this.u.width || s.height !== this.u.height) this.u.setSize(s);
    if (this.boxes) this.boxes.forEach((b) => b.place());
  };

  /**
   * Asks for the envelopes of the current window at the plot's width in
   * device pixels, once per frame and only when the window or width changed;
   * a reply overtaken by a newer request is dropped.
   */
  SvzPlot.prototype.requestEnvelope = function () {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => {
      this.pending = false;
      const u = this.u;
      if (!u || u.scales.x.min == null) return;
      const width = Math.round(u.bbox.width);
      const key = `${u.scales.x.min}|${u.scales.x.max}|${width}`;
      if (key === this.lastWindow) return;
      this.lastWindow = key;
      const seq = ++this.seq;
      const t0 = performance.now();
      Promise.resolve(this.opts.query("envelope", { t0: u.scales.x.min, t1: u.scales.x.max, width }))
        .then((env) => {
          if (seq !== this.seq || u !== this.u || !env) return;
          const data = align(this.spec, env.traces);
          // setData(..., false) keeps the scales but not the visible index
          // ranges, which uPlot recomputes only when a scale is set: set the
          // same window again (requestEnvelope ignores it, as unchanged).
          const { min, max } = u.scales.x;
          u.setData(data, false);
          u.setScale("x", { min, max });
          if (this.opts.onStats) {
            this.opts.onStats({ engineMs: env.ms, totalMs: performance.now() - t0, points: data[0].length });
          }
        });
    });
  };

  /** Pan on drag, zoom on the wheel (time only), cursors on double-click. */
  SvzPlot.prototype.installMouse = function () {
    const u = this.u, over = u.over;
    let drag = null;
    over.addEventListener("pointerdown", (ev) => {
      if (ev.button !== 0) return;
      drag = { x: ev.clientX, min: u.scales.x.min, max: u.scales.x.max };
      over.setPointerCapture(ev.pointerId);
    });
    over.addEventListener("pointermove", (ev) => {
      if (!drag) return;
      const per = (drag.max - drag.min) / over.clientWidth;
      const dx = (ev.clientX - drag.x) * per;
      u.setScale("x", { min: drag.min - dx, max: drag.max - dx });
    });
    const end = () => { drag = null; };
    over.addEventListener("pointerup", end);
    over.addEventListener("pointercancel", end);
    over.addEventListener("wheel", (ev) => {
      ev.preventDefault();
      const at = u.posToVal(ev.offsetX, "x");
      const f = Math.exp(ev.deltaY / 500);
      const min = at - (at - u.scales.x.min) * f, max = at + (u.scales.x.max - at) * f;
      if (max - min > 1e-6) u.setScale("x", { min, max });
    }, { passive: false });
    over.addEventListener("dblclick", (ev) => {
      const t = u.posToVal(ev.offsetX, "x");
      if (ev.shiftKey) this.cursors.right = t; else this.cursors.left = t;
      this.cursors.visible = true;
      u.redraw(false);
    });
    over.addEventListener("pointerleave", () => { this.tipEl.hidden = true; if (this.opts.onHover) this.opts.onHover(null); });
  };

  /** The metadata box and the legend, draggable. */
  SvzPlot.prototype.installBoxes = function (spec) {
    const over = this.u.over;
    const meta = Kit.el("div", { class: "svz-box svz-meta" },
      spec.metadata.map(([k, v]) => Kit.el("div", { class: "svz-meta-row" }, Kit.el("span", {}, k), Kit.el("span", {}, v))));
    const legend = Kit.el("div", { class: "svz-box svz-legend" },
      spec.traces.map((tr) => Kit.el("div", { class: "svz-legend-row" },
        Kit.el("span", { class: "svz-swatch", style: { background: tr.color } }), tr.name)));
    meta.hidden = !spec.metadata.length;
    over.append(meta, legend);
    this.boxes = [draggable(meta, over, this.metaPos || [0.02, 0.03]), draggable(legend, over, [0.72, 0.03])];
    this.boxEls = [meta, legend];
    requestAnimationFrame(() => this.boxes.forEach((b) => b.place()));
  };

  /** Cursors and event markers, over the series. */
  SvzPlot.prototype.drawOverlays = function (u) {
    const ctx = u.ctx, { left, top, width, height } = u.bbox;
    // Device pixels per CSS pixel: the screen's, or a print chart's scale.
    const k = u.svzScale || devicePixelRatio;
    const line = (t, color, dash) => {
      if (t == null) return;
      const x = Math.round(u.valToPos(t, "x", true)) + 0.5;
      if (x < left || x > left + width) return;
      ctx.save();
      ctx.strokeStyle = color;
      ctx.lineWidth = k;
      ctx.setLineDash(dash.map((d) => d * k));
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, top + height);
      ctx.stroke();
      ctx.restore();
    };
    for (const m of this.spec.markers) line(m, "#888", [4, 4]);
    if (this.cursors.visible) {
      line(this.cursors.left, "#d62728", [6, 3]);
      line(this.cursors.right, "#1f77b4", [6, 3]);
    }
  };

  /** The hover tooltip, from the engine's readout at the mouse's time. */
  SvzPlot.prototype.hover = function (u) {
    if (u.cursor.left == null || u.cursor.left < 0) return;
    const t = u.posToVal(u.cursor.left, "x");
    const x = u.cursor.left, y = u.cursor.top;
    if (this.opts.onHover) this.opts.onHover(t);
    const seq = (this.hoverSeq = (this.hoverSeq || 0) + 1);
    Promise.resolve(this.opts.query("hover", { t })).then((h) => {
      if (seq !== this.hoverSeq || !h) return;
      this.tipEl.replaceChildren(Kit.el("div", { class: "svz-tip-time" }, h.time),
        ...h.rows.map((r) => Kit.el("div", {}, Kit.el("span", { class: "svz-swatch", style: { background: r.color } }), r.text)));
      const o = u.over.getBoundingClientRect(), hr = this.host.getBoundingClientRect();
      this.tipEl.style.left = `${o.left - hr.left + x + 14}px`;
      this.tipEl.style.top = `${o.top - hr.top + y + 14}px`;
      this.tipEl.hidden = false;
    });
  };

  /**
   * Puts the cursors near the edges of the view (as a rebuild does in the Qt
   * app): of `range`, or of the current window. A new plot passes its range,
   * since uPlot applies a first setScale only once it has data.
   */
  SvzPlot.prototype.resetCursors = function (range) {
    if (!this.u) return;
    const [min, max] = range || [this.u.scales.x.min, this.u.scales.x.max];
    const span = max - min;
    this.cursors.left = min + span * 0.1;
    this.cursors.right = max - span * 0.1;
    this.u.redraw(false);
  };

  /** View > Zoom to Cursors: the time between the cursors, when shown and ordered. */
  SvzPlot.prototype.zoomToCursors = function () {
    const c = this.cursors;
    if (!this.u || !c.visible || c.left == null || c.right == null || c.right <= c.left) return;
    this.u.setScale("x", { min: c.left, max: c.right });
  };

  /** View > Show Cursors. */
  SvzPlot.prototype.setCursorsVisible = function (on) {
    this.cursors.visible = on;
    if (on) this.resetCursors(); else if (this.u) this.u.redraw(false);
  };

  /** View > Reset Zoom: the full data range plus 5%, cursors reset. */
  SvzPlot.prototype.resetZoom = function () {
    if (!this.u || !this.spec) return;
    const [a, b] = this.spec.xDataRange;
    const pad = (b - a) * 0.05 || 1;
    this.u.setScale("x", { min: a - pad, max: b + pad });
    this.resetCursors([a - pad, b + pad]);
  };

  /** Page sizes in millimetres, landscape. */
  const PAGES = { a4: [297, 210], letter: [279.4, 215.9] };

  /**
   * File > Export PDF: the current view on one landscape page, as a PDF.
   *
   * The chart -- traces, axes, grid, cursors and markers -- is drawn again
   * offscreen by uPlot at the page's size and `dpi`, from envelopes fetched
   * for that width (one column per output pixel, as the Qt app refills for
   * its printer), and placed as one image. The title, the metadata box and
   * the legend, which are HTML on screen, are written as PDF text at the
   * same places, so they stay sharp at any zoom.
   *
   * @param {Object} [o] `page` ("a4" or "letter"), `dpi` (default 300),
   *     `margin` in mm (default 10).
   * @returns {Promise<Object>} The jsPDF document; `.output("arraybuffer")`
   *     for the engine to write, or `.save(name)` in a browser.
   */
  SvzPlot.prototype.exportPdf = async function (o) {
    o = Object.assign({ page: "a4", dpi: 300, margin: 10 }, o || {});
    const spec = this.spec;
    const [pw, ph] = PAGES[o.page];
    const titleH = spec.title ? 9 : 0;
    const cw = pw - 2 * o.margin, ch = ph - 2 * o.margin - titleH;
    // The chart in output pixels at `dpi`, drawn at pixel ratio 1 with every
    // size scaled by k = dpi / 96, so it looks like the screen's at 96 per
    // inch; its pixels map to millimetres by mm().
    const k = o.dpi / 96;
    const pxW = Math.round(cw / 25.4 * o.dpi), pxH = Math.round(ch / 25.4 * o.dpi);
    const mm = (px) => px / o.dpi * 25.4;
    const holder = Kit.el("div", { style: { position: "fixed", left: "-100000px", top: "0" } });
    document.body.append(holder);
    const self = this;
    const u = new uPlot(Object.assign(chartOptions(spec, true, k / devicePixelRatio), {
      width: Math.round(pxW / devicePixelRatio), height: Math.round(pxH / devicePixelRatio),
      cursor: { show: false },
      hooks: { draw: [(p) => self.drawOverlays(p)] },
    }), [[], ...spec.traces.map(() => [])], holder);
    u.svzScale = k;
    const { min, max } = this.u.scales.x;
    u.setScale("x", { min, max });
    const env = await this.opts.query("envelope", { t0: min, t1: max, width: Math.round(u.bbox.width) });
    u.setData(align(spec, env.traces), false);
    u.setScale("x", { min, max });
    // uPlot commits changes in a microtask and lays out and draws after it:
    // wait two frames before reading the canvas and the plot area.
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
    const image = u.ctx.canvas.toDataURL("image/png");

    const doc = new jspdf.jsPDF({ orientation: "landscape", unit: "mm", format: o.page });
    if (spec.title) {
      doc.setFont("helvetica", "bold").setFontSize(12);
      doc.text(spec.title, pw / 2, o.margin + 5, { align: "center" });
    }
    const top = o.margin + titleH;
    // Compressed: a mostly white chart deflates to a small fraction.
    doc.addImage(image, "PNG", o.margin, top, cw, ch, undefined, "FAST");
    // The boxes, at their places on screen as fractions of the plot area,
    // which uPlot's bbox gives in canvas pixels, here output pixels.
    const scaleX = cw / mm(u.ctx.canvas.width), scaleY = ch / mm(u.ctx.canvas.height);
    const area = { x: o.margin + mm(u.bbox.left) * scaleX, y: top + mm(u.bbox.top) * scaleY,
      w: mm(u.bbox.width) * scaleX, h: mm(u.bbox.height) * scaleY };
    const box = (pos, rows, mono) => {
      const lineH = 4.2, pad = 2;
      doc.setFont(mono ? "courier" : "helvetica", "normal").setFontSize(8);
      const textW = Math.max(...rows.map((r) => doc.getTextWidth(r.text) + (r.color ? 9 : 0)));
      const x = area.x + pos.fx * area.w, y = area.y + pos.fy * area.h;
      const w = textW + 2 * pad, h = rows.length * lineH + 2 * pad;
      doc.setFillColor(255, 255, 255).setDrawColor(150).setLineWidth(0.2).rect(x, y, w, h, "FD");
      rows.forEach((r, i) => {
        const ty = y + pad + lineH * (i + 0.75);
        let tx = x + pad;
        if (r.color) {
          doc.setDrawColor(r.color).setLineWidth(0.8).line(tx, ty - 1.1, tx + 6, ty - 1.1);
          tx += 9;
        }
        doc.setTextColor(0).text(r.text, tx, ty);
      });
    };
    if (spec.metadata.length && !this.boxEls[0].hidden) {
      box(this.boxes[0].pos, spec.metadata.map(([k, v]) => ({ text: `${k.padEnd(8)}${v}` })), true);
    }
    box(this.boxes[1].pos, spec.traces.map((tr) => ({ text: tr.name, color: tr.color })), false);
    u.destroy();
    holder.remove();
    return doc;
  };

  window.SvzPlot = SvzPlot;
})();
