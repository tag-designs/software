/**
 * @file
 * The sensorViz plot spike's window: the menu bar, the Plot and File Info
 * tabs, the plot (plot.js) and a status line with each redraw's timings.
 * Runs against MockEngine. ?log=imutag|compasstag|big picks the log,
 * ?nopanel hides the switcher, ?theme=dark|light forces a theme.
 */
(function () {
  "use strict";

  const params = Kit.setupPage("sensorViz");
  const engine = Kit.createEngine(!Kit.inApp() && window.MockEngine ? MockEngine.create(params) : null);
  const $ = (id) => document.getElementById(id);
  let state = null;
  let stats = "";
  let breakAtGaps = true;
  let utcOffset = 0;

  const plot = new SvzPlot($("plot"), {
    query: (name, args) => engine.query(name, args),
    onStats: (s) => {
      stats = `redraw: ${s.points.toLocaleString()} points, engine ${s.engineMs.toFixed(1)} ms, total ${s.totalMs.toFixed(1)} ms`;
      showStatus();
    },
  });

  /** The status line: the engine's load message and the last redraw. */
  function showStatus() {
    $("status").textContent = [state && state.status, stats].filter(Boolean).join("   ·   ");
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

  Kit.menuBar($("menubar"), [
    { title: "File", items: () => [
      { label: "Load", items: () => ["imutag", "compasstag", "big"].map((n) => ({ label: n, action: () => engine.command("load", { log: n }) })) },
      "-",
      { label: "Export PDF...", action: async () => {
        const t0 = performance.now();
        const doc = await plot.exportPdf();
        stats = `PDF in ${(performance.now() - t0).toFixed(0)} ms`;
        showStatus();
        doc.save(`${(state && state.spec && state.spec.title) || "sensorviz"}.pdf`);
      } },
    ] },
    { title: "View", items: () => [
      { label: "Reset Zoom", action: () => plot.resetZoom() },
      { label: "Zoom to Cursors", action: () => plot.zoomToCursors() },
      { label: "Show Cursors", checked: plot.cursors.visible, action: () => plot.setCursorsVisible(!plot.cursors.visible) },
      "-",
      { label: "Break Lines at Gaps", checked: breakAtGaps, action: () => {
        breakAtGaps = !breakAtGaps;
        engine.command("set_break_at_gaps", { on: breakAtGaps });
      } },
    ] },
    { title: "Configuration", items: () => [
      { label: `UTC Offset... (${utcOffset})`, action: async () => {
        const text = await Kit.promptText("UTC Offset", "Hours from UTC", String(utcOffset));
        if (text !== null && !Number.isNaN(parseInt(text, 10))) {
          utcOffset = parseInt(text, 10);
          engine.command("set_utc_offset", { hours: utcOffset });
        }
      } },
    ] },
  ]);

  engine.on("state", (s) => {
    const changed = !state || JSON.stringify(state.spec) !== JSON.stringify(s.spec);
    state = s;
    if (changed) plot.setSpec(s.spec);
    $("info").textContent = s.log ? s.log.info.map(([k, v]) => `${k}: ${v}`).join("\n") : "";
    showStatus();
  }).then(() => engine.command("ready"));
})();
