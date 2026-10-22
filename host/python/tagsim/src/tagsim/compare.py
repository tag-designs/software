"""A side-by-side page of the true and estimated attitude and altitude.

``tagsim compare`` writes one HTML file that shows, on a shared timeline:

- two 3D models of the airframe, truth on the left and the estimate on the
  right, seen from the same camera (drag either view to turn both);
- a pair of vertical bars with the true and estimated altitude above the
  start;
- strips of the attitude error (tilt and heading) and the altitude error
  over the whole flight, with the current time marked; clicking a strip
  jumps there, and ``#t=SECONDS`` on the URL opens the page at that time.

It shows only what the filter estimates. There is no horizontal track,
because the filter has no horizontal position (see the design notes).

The page carries its data inline (gzip-compressed) and loads three.js from a CDN, so it opens
from a local file in any current browser. The data includes the flying
site's attitude and altitude history, so keep the file with the other
generated output, outside the repository.

Frames: the attitude shown is the airframe's (body FRD to NED, through the
run's mounting), converted here to three.js's world frame (x east, y up,
z south) and to the model's own axes (x forward, y up, z right), so the page
only applies quaternions.
"""

from __future__ import annotations

import base64
import gzip
import json
import sqlite3
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

from .score import load_truth, truth_at
from .viewer import body_euler_deg, body_q_ned

#: NED vectors to three.js world (x east, y up, z south).
_NED_TO_THREE = np.array([[0.0, 1.0, 0.0],
                          [0.0, 0.0, -1.0],
                          [-1.0, 0.0, 0.0]])
#: The model's axes (x forward, y up, z right) to body FRD.
_MODEL_TO_FRD = np.array([[1.0, 0.0, 0.0],
                          [0.0, 0.0, 1.0],
                          [0.0, -1.0, 0.0]])


def three_quaternions(q_enu_tag: np.ndarray, mounting: str) -> np.ndarray:
    """Model-to-world quaternions (x y z w, three.js order) of the airframe."""
    q_nb = body_q_ned(q_enu_tag, mounting)
    r_nb = Rotation.from_quat(np.concatenate([q_nb[:, 1:], q_nb[:, :1]], axis=1)).as_matrix()
    r = _NED_TO_THREE @ r_nb @ _MODEL_TO_FRD
    return Rotation.from_matrix(r).as_quat()


def compare_data(truth_path: Path, score_path: Path, rate_hz: float | None = None) -> dict:
    """The page's data: truth and estimate at ``rate_hz`` on the score's rows.

    By default the rate is the source flight log's own attitude rate (about
    25 Hz for the ArduPilot logs), so the page shows no more samples per
    second than the trajectory was recorded at.
    """
    truth = load_truth(truth_path)
    if rate_hz is None:
        rate_hz = 1.0 / truth.config["trajectory"]["dt"]
    mounting = truth.config["sensor"]["mounting"]
    con = sqlite3.connect(score_path)
    try:
        rows = np.array(con.execute(
            "SELECT ElapsedUs, Flying, qw, qx, qy, qz, TiltDeg, HeadingDeg, AltitudeErrM "
            "FROM ScoreSeries ORDER BY ElapsedUs").fetchall(), dtype=np.float64)
        estimate = con.execute("SELECT Estimate FROM ScoreRun").fetchone()[0]
    finally:
        con.close()
    rows = rows[np.all(np.isfinite(rows[:, 2:6]), axis=1)]
    step = max(1, int(round(1e6 / rate_hz / np.median(np.diff(rows[:, 0])))))
    rows = rows[::step]
    t_us = rows[:, 0]
    q_true, _ = truth_at(truth, t_us)
    q_est = rows[:, 2:6]
    up = np.interp(t_us, truth.elapsed_us, truth.up_m)
    alt_true = up - up[0]
    alt_err = np.nan_to_num(rows[:, 8])
    has_alt = bool(np.isfinite(rows[:, 8]).any())

    def r(a, n):
        return np.round(np.asarray(a, dtype=np.float64), n).tolist()

    return {
        "rateHz": float(1e6 / np.median(np.diff(t_us))),
        "title": f"{Path(truth_path).name.removesuffix('.truth.db3')}: truth and {estimate}",
        "estimate": estimate,
        "t": r((t_us - t_us[0]) * 1e-6, 3),
        "flying": rows[:, 1].astype(int).tolist(),
        "qTrue": r(three_quaternions(q_true, mounting), 4),
        "qEst": r(three_quaternions(q_est, mounting), 4),
        "eulerTrue": r(body_euler_deg(q_true, mounting), 1),
        "eulerEst": r(body_euler_deg(q_est, mounting), 1),
        "altTrue": r(alt_true, 3),
        "altEst": r(alt_true + alt_err, 3) if has_alt else None,
        "tiltErr": r(rows[:, 6], 3),
        "headingErr": r(rows[:, 7], 3),
        "altErr": r(alt_err, 4) if has_alt else None,
    }


def write_compare_page(path: Path, data: dict) -> None:
    """Write the page with ``data`` (from :func:`compare_data`) inline.

    The data is gzip-compressed JSON in base64 (about a third of the plain
    JSON's size), which the page inflates with the browser's own
    ``DecompressionStream``.
    """
    raw = json.dumps(data, separators=(",", ":")).encode()
    payload = base64.b64encode(gzip.compress(raw, mtime=0)).decode()
    html = _PAGE.replace("__TITLE__", data["title"]).replace("__DATA__", payload)
    Path(path).write_text(html, encoding="utf-8")


_PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Attitude Comparison</title>
<style>
:root {
  --bg: #f6f7f9; --panel: #ffffff; --ink: #1d2330; --muted: #5d6677; --line: #d8dce3;
  --truth: #2f6fdb; --est: #d9622b; --tilt: #2f6fdb; --heading: #8a4fd1; --alt: #2a9d6f;
  --flying: rgba(47, 111, 219, 0.07); --cursor: #c0392b;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #14171c; --panel: #1c2027; --ink: #e6e9ee; --muted: #9aa3b2; --line: #333a45;
    --truth: #6b9cf0; --est: #f08a57; --tilt: #6b9cf0; --heading: #b58cf0; --alt: #4cc497;
    --flying: rgba(107, 156, 240, 0.10); --cursor: #ff6b5b;
  }
}
:root[data-theme="dark"] {
  --bg: #14171c; --panel: #1c2027; --ink: #e6e9ee; --muted: #9aa3b2; --line: #333a45;
  --truth: #6b9cf0; --est: #f08a57; --tilt: #6b9cf0; --heading: #b58cf0; --alt: #4cc497;
  --flying: rgba(107, 156, 240, 0.10); --cursor: #ff6b5b;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
       font: 14px/1.4 system-ui, -apple-system, "Segoe UI", sans-serif; }
header { padding: 12px 16px 4px; }
h1 { font-size: 16px; margin: 0; font-weight: 600; }
.note { color: var(--muted); font-size: 12px; margin-top: 2px; }
main { padding: 8px 16px 16px; display: grid; gap: 10px; }
.views { display: grid; grid-template-columns: 1fr 120px 1fr; gap: 10px; min-height: 0; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; overflow: hidden; }
.view { position: relative; height: min(46vh, 460px); }
.view canvas { display: block; width: 100%; height: 100%; touch-action: none; }
.label { position: absolute; left: 10px; top: 8px; font-weight: 600; }
.readout { position: absolute; left: 10px; bottom: 8px; font: 12px ui-monospace, Menlo, monospace;
           color: var(--muted); white-space: pre; }
.truth .label { color: var(--truth); } .est .label { color: var(--est); }
.bars { display: flex; flex-direction: column; padding: 8px 6px; }
.bars .title { text-align: center; font-size: 12px; color: var(--muted); }
.barbox { flex: 1; display: flex; gap: 8px; justify-content: center; position: relative; margin: 6px 0; }
.bar { width: 26px; position: relative; background: var(--bg); border: 1px solid var(--line); border-radius: 4px; }
.fill { position: absolute; left: 0; right: 0; bottom: 0; border-radius: 3px; }
.bar.t .fill { background: var(--truth); } .bar.e .fill { background: var(--est); }
.bars .vals { font: 11px ui-monospace, Menlo, monospace; text-align: center; color: var(--muted); }
.controls { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; padding: 8px 10px; }
.controls input[type=range] { flex: 1; min-width: 160px; }
button, select { font: inherit; color: var(--ink); background: var(--bg); border: 1px solid var(--line);
                 border-radius: 6px; padding: 4px 10px; cursor: pointer; }
.time { font: 13px ui-monospace, Menlo, monospace; min-width: 110px; }
.strips canvas { display: block; width: 100%; height: 210px; cursor: crosshair; }
.legend { display: flex; flex-wrap: wrap; gap: 14px; padding: 6px 10px 8px; font-size: 12px; color: var(--muted); }
.sw { display: inline-block; width: 12px; height: 3px; vertical-align: middle; margin-right: 5px; }
@media (max-width: 760px) {
  .views { grid-template-columns: 1fr 1fr; }
  .bars { grid-column: 1 / -1; order: 3; flex-direction: row; align-items: center; height: 120px; }
  .view { height: 38vh; }
}
</style>
<script type="importmap">
{ "imports": {
  "three": "https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js",
  "three/addons/": "https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"
} }
</script>
</head>
<body>
<header>
  <h1>__TITLE__</h1>
  <div class="note">Airframe attitude and altitude only; no horizontal track. Drag a view to turn both cameras.</div>
</header>
<main>
  <div class="views">
    <div class="panel view truth"><canvas id="cvTrue"></canvas><div class="label">Truth</div><div class="readout" id="roTrue"></div></div>
    <div class="panel bars">
      <div class="title">Altitude above start</div>
      <div class="barbox"><div class="bar t"><div class="fill" id="barT"></div></div><div class="bar e"><div class="fill" id="barE"></div></div></div>
      <div class="vals" id="altVals"></div>
    </div>
    <div class="panel view est"><canvas id="cvEst"></canvas><div class="label" id="estLabel">Estimate</div><div class="readout" id="roEst"></div></div>
  </div>
  <div class="panel controls">
    <button id="play">Play</button>
    <select id="speed"><option>0.25</option><option>0.5</option><option selected>1</option><option>2</option><option>4</option><option>8</option></select>
    <input type="range" id="slider" min="0" value="0" step="1">
    <span class="time" id="time"></span>
  </div>
  <div class="panel strips">
    <canvas id="strip"></canvas>
    <div class="legend">
      <span><span class="sw" style="background:var(--tilt)"></span>tilt error (deg)</span>
      <span><span class="sw" style="background:var(--heading)"></span>heading error (deg)</span>
      <span id="altLegend"><span class="sw" style="background:var(--alt)"></span>altitude error (cm)</span>
      <span><span class="sw" style="background:var(--flying);height:10px"></span>flying</span>
    </div>
  </div>
</main>
<script type="text/plain" id="data">__DATA__</script>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

async function inflate(b64) {
  const bytes = Uint8Array.from(atob(b64.trim()), c => c.charCodeAt(0));
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return JSON.parse(await new Response(stream).text());
}
const D = await inflate(document.getElementById('data').textContent);
const N = D.t.length;
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
document.getElementById('estLabel').textContent = 'Estimate: ' + D.estimate;

// --- 3D views -------------------------------------------------------------
function airframe(accent) {
  const g = new THREE.Group();
  const top = new THREE.MeshStandardMaterial({ color: 0xe8e8e8, roughness: 0.6 });
  const under = new THREE.MeshStandardMaterial({ color: 0x3a3f4a, roughness: 0.7 });
  const acc = new THREE.MeshStandardMaterial({ color: accent, roughness: 0.5 });
  const box = (w, h, d, mat, x, y, z) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat); m.position.set(x, y, z); g.add(m); return m;
  };
  box(3.0, 0.32, 0.36, top, 0, 0, 0);                    // fuselage, x forward
  box(0.9, 0.06, 3.6, top, 0.15, 0.10, 0);               // wing, light top
  box(0.9, 0.04, 3.6, under, 0.15, 0.05, 0);             // wing underside, dark
  box(0.12, 0.08, 0.5, acc, 0.15, 0.13, 1.55);           // right wing tip marker
  box(0.5, 0.05, 1.3, top, -1.3, 0.1, 0);                // tailplane
  box(0.5, 0.7, 0.06, acc, -1.3, 0.45, 0);               // fin, up
  const nose = new THREE.Mesh(new THREE.ConeGeometry(0.2, 0.5, 16), acc);
  nose.rotation.z = -Math.PI / 2; nose.position.set(1.75, 0, 0); g.add(nose);
  return g;
}

function compassRose(scene) {
  const grid = new THREE.GridHelper(12, 12, 0x888888, 0x888888);
  grid.position.y = -2.2; grid.material.opacity = 0.35; grid.material.transparent = true; scene.add(grid);
  const label = (text, x, z) => {
    const c = document.createElement('canvas'); c.width = c.height = 64;
    const k = c.getContext('2d'); k.fillStyle = '#888'; k.font = 'bold 44px system-ui';
    k.textAlign = 'center'; k.textBaseline = 'middle'; k.fillText(text, 32, 34);
    const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c) }));
    s.position.set(x, -2.0, z); s.scale.set(0.8, 0.8, 0.8); scene.add(s);
  };
  label('N', 0, -6.3); label('S', 0, 6.3); label('E', 6.3, 0); label('W', -6.3, 0);
}

function makeView(canvas, accent) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  const scene = new THREE.Scene();
  scene.add(new THREE.HemisphereLight(0xffffff, 0x444444, 1.6));
  const sun = new THREE.DirectionalLight(0xffffff, 1.4); sun.position.set(3, 8, 4); scene.add(sun);
  compassRose(scene);
  const model = airframe(accent); scene.add(model);
  const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 100);
  camera.position.set(6, 4, 8);
  const controls = new OrbitControls(camera, canvas);
  controls.enablePan = false;
  return { renderer, scene, camera, controls, model, canvas };
}
const vT = makeView(document.getElementById('cvTrue'), css('--truth'));
const vE = makeView(document.getElementById('cvEst'), css('--est'));
// One camera pose for both: whichever view is dragged drives the other.
let syncing = false;
function sync(from, to) {
  if (syncing) return; syncing = true;
  to.camera.position.copy(from.camera.position); to.camera.quaternion.copy(from.camera.quaternion);
  to.controls.target.copy(from.controls.target); to.controls.update();
  syncing = false; draw();
}
vT.controls.addEventListener('change', () => sync(vT, vE));
vE.controls.addEventListener('change', () => sync(vE, vT));

function resize() {
  for (const v of [vT, vE]) {
    const w = v.canvas.clientWidth, h = v.canvas.clientHeight;
    v.renderer.setSize(w, h, false); v.camera.aspect = w / h; v.camera.updateProjectionMatrix();
  }
  sizeStrip(); draw();
}

// --- altitude bars ----------------------------------------------------------
const alts = D.altTrue.concat(D.altEst || []);
const aMin = Math.min(0, ...alts), aMax = Math.max(...alts);
const barT = document.getElementById('barT'), barE = document.getElementById('barE');
const pct = a => (100 * (a - aMin) / ((aMax - aMin) || 1)).toFixed(2) + '%';

// --- strips -----------------------------------------------------------------
const strip = document.getElementById('strip');
const sctx = strip.getContext('2d');
let stripImage = null;
const tEnd = D.t[N - 1];
const series = [
  { v: D.tiltErr, color: '--tilt', unit: 'deg', name: 'tilt' },
  { v: D.headingErr, color: '--heading', unit: 'deg', name: 'heading' },
];
if (D.altErr) series.push({ v: D.altErr.map(x => 100 * x), color: '--alt', unit: 'cm', name: 'altitude' });
else document.getElementById('altLegend').style.display = 'none';

function sizeStrip() {
  const dpr = window.devicePixelRatio || 1;
  strip.width = strip.clientWidth * dpr; strip.height = strip.clientHeight * dpr;
  sctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  renderStrips();
}
const PADL = 44, PADR = 8;
function renderStrips() {
  const W = strip.clientWidth, H = strip.clientHeight, rows = series.length, rh = H / rows;
  sctx.clearRect(0, 0, W, H);
  const x = t => PADL + (W - PADL - PADR) * t / tEnd;
  // flying shading
  sctx.fillStyle = css('--flying');
  let s = null;
  for (let i = 0; i <= N; i++) {
    const f = i < N && D.flying[i];
    if (f && s === null) s = i;
    if (!f && s !== null) { sctx.fillRect(x(D.t[s]), 0, x(D.t[i - 1]) - x(D.t[s]), H); s = null; }
  }
  sctx.font = '11px system-ui'; sctx.textBaseline = 'middle';
  series.forEach((ser, r) => {
    const y0 = r * rh, v = ser.v;
    let lo = Math.min(...v), hi = Math.max(...v);
    if (lo > 0) lo = 0;   // unsigned errors start at zero; signed ones keep their sign
    if (hi - lo < 1e-9) hi = lo + 1;
    const y = val => y0 + rh - 4 - (rh - 10) * (val - lo) / (hi - lo);
    sctx.strokeStyle = css('--line'); sctx.lineWidth = 1;
    sctx.beginPath(); sctx.moveTo(PADL, y0 + rh - 0.5); sctx.lineTo(W - PADR, y0 + rh - 0.5); sctx.stroke();
    if (lo < 0 && hi > 0) { sctx.setLineDash([3, 3]); sctx.beginPath(); sctx.moveTo(PADL, y(0)); sctx.lineTo(W - PADR, y(0)); sctx.stroke(); sctx.setLineDash([]); }
    sctx.fillStyle = css('--muted'); sctx.textAlign = 'right';
    sctx.fillText(hi.toFixed(hi < 10 ? 2 : 1), PADL - 4, y(hi));
    sctx.fillText(lo.toFixed(Math.abs(lo) < 10 ? 2 : 1), PADL - 4, y(lo));
    // min/max per pixel column, so long flights stay honest
    sctx.strokeStyle = css(ser.color); sctx.beginPath();
    const cols = Math.max(1, Math.floor(W - PADL - PADR));
    let i = 0;
    for (let c = 0; c < cols; c++) {
      const tc = tEnd * (c + 1) / cols;
      let mn = Infinity, mx = -Infinity;
      while (i < N && D.t[i] <= tc) { mn = Math.min(mn, v[i]); mx = Math.max(mx, v[i]); i++; }
      if (mn === Infinity) continue;
      sctx.moveTo(PADL + c + 0.5, y(mn)); sctx.lineTo(PADL + c + 0.5, y(mx) - 0.01);
    }
    sctx.stroke();
  });
  stripImage = sctx.getImageData(0, 0, strip.width, strip.height);
}
function drawCursor(k) {
  if (!stripImage) return;
  sctx.putImageData(stripImage, 0, 0);
  const W = strip.clientWidth, H = strip.clientHeight;
  const xc = PADL + (W - PADL - PADR) * D.t[k] / tEnd;
  sctx.strokeStyle = css('--cursor'); sctx.lineWidth = 1.5;
  sctx.beginPath(); sctx.moveTo(xc, 0); sctx.lineTo(xc, H); sctx.stroke();
}
strip.addEventListener('pointerdown', e => {
  const r = strip.getBoundingClientRect();
  const t = tEnd * (e.clientX - r.left - PADL) / (r.width - PADL - PADR);
  seek(indexAt(Math.max(0, Math.min(tEnd, t))));
});

// --- timeline -----------------------------------------------------------------
const slider = document.getElementById('slider'); slider.max = N - 1;
const timeEl = document.getElementById('time'), playBtn = document.getElementById('play');
const speedSel = document.getElementById('speed');
let k = 0, playing = false, tNow = 0, last = null;
function indexAt(t) {   // last sample at or before t
  let lo = 0, hi = N - 1;
  while (lo < hi) { const m = (lo + hi + 1) >> 1; if (D.t[m] <= t) lo = m; else hi = m - 1; }
  return lo;
}
const fmt = s => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
const eul = e => `roll ${e[0].toFixed(1).padStart(6)}  pitch ${e[1].toFixed(1).padStart(6)}  yaw ${e[2].toFixed(1).padStart(6)}`;
function seek(i) { k = i; tNow = D.t[k]; slider.value = k; draw(); }
function draw() {
  const qa = D.qTrue[k], qb = D.qEst[k];
  vT.model.quaternion.set(qa[0], qa[1], qa[2], qa[3]);
  vE.model.quaternion.set(qb[0], qb[1], qb[2], qb[3]);
  for (const v of [vT, vE]) v.renderer.render(v.scene, v.camera);
  barT.style.height = pct(D.altTrue[k]);
  if (D.altEst) barE.style.height = pct(D.altEst[k]);
  document.getElementById('altVals').textContent =
    `${D.altTrue[k].toFixed(2)} m\n${D.altEst ? D.altEst[k].toFixed(2) + ' m' : '–'}`;
  document.getElementById('roTrue').textContent = eul(D.eulerTrue[k]);
  document.getElementById('roEst').textContent = eul(D.eulerEst[k]) +
    `\ntilt err ${D.tiltErr[k].toFixed(2)}°  heading err ${D.headingErr[k].toFixed(2)}°` +
    (D.altErr ? `  alt err ${(100 * D.altErr[k]).toFixed(1)} cm` : '');
  timeEl.textContent = `${fmt(D.t[k])} / ${fmt(tEnd)}`;
  drawCursor(k);
}
document.getElementById('altVals').style.whiteSpace = 'pre';
slider.addEventListener('input', () => { playing && toggle(); seek(+slider.value); });
function toggle() { playing = !playing; playBtn.textContent = playing ? 'Pause' : 'Play'; last = null; if (playing) requestAnimationFrame(tick); }
playBtn.addEventListener('click', () => { if (k >= N - 1) seek(0); toggle(); });
document.addEventListener('keydown', e => {
  if (e.target.tagName === 'SELECT') return;
  if (e.code === 'Space') { e.preventDefault(); toggle(); }
  if (e.code === 'ArrowRight') seek(Math.min(N - 1, k + 1));
  if (e.code === 'ArrowLeft') seek(Math.max(0, k - 1));
});
function tick(now) {
  if (!playing) return;
  if (last !== null) tNow += (now - last) / 1000 * parseFloat(speedSel.value);
  last = now;
  if (tNow >= tEnd) { tNow = tEnd; seek(N - 1); toggle(); return; }
  const i = indexAt(tNow);
  if (i !== k) { k = i; slider.value = k; draw(); }
  requestAnimationFrame(tick);
}
window.addEventListener('resize', resize);
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { renderStrips(); draw(); });
resize();
// #t=SECONDS opens the page at that time.
const h = /t=([\d.]+)/.exec(location.hash);
seek(h ? indexAt(Math.min(tEnd, parseFloat(h[1]))) : 0);
</script>
</body>
</html>
"""


__all__ = ["compare_data", "write_compare_page", "three_quaternions"]
