//! Spike: sensorviz's IMUTag plot drawn by plotters for printing.
//!
//! Reads the IMUTag fixture log, derives the four traces sensorviz shows by
//! default (gyro, acceleration and magnetic-field magnitudes, and altitude),
//! takes each one's M4 envelope at print resolution, and draws the plot --
//! four stacked y axes on the left, each in its trace's colour, the elapsed
//! time axis, grid lines, the title, the metadata box and the legend -- with
//! plotters' drawing primitives, since its chart builder has only a primary
//! and a secondary y axis. Writes SVG and PNG.
//!
//!     cargo run --release -- <imutag.db3> <out-prefix>

use plotters::prelude::*;
use plotters::style::text_anchor::{HPos, Pos, VPos};

/// A trace: times (s), values, and how it is drawn.
struct Trace {
    /// Axis label: name and units.
    label: &'static str,
    /// Line colour.
    color: RGBColor,
    /// Sample times, s.
    t: Vec<f64>,
    /// Sample values.
    v: Vec<f64>,
}

/// Reads `(time s, columns...)` rows from an IMUTag table, ElapsedUs as seconds.
fn read(db: &rusqlite::Connection, table: &str, cols: &[&str]) -> (Vec<f64>, Vec<Vec<f64>>) {
    let sql = format!("select ElapsedUs, {} from {table} order by rowid", cols.join(", "));
    let mut stmt = db.prepare(&sql).expect("prepare");
    let mut t = Vec::new();
    let mut v = vec![Vec::new(); cols.len()];
    let mut rows = stmt.query([]).expect("query");
    while let Some(row) = rows.next().expect("row") {
        t.push(row.get::<_, f64>(0).expect("time") * 1e-6);
        for (k, col) in v.iter_mut().enumerate() {
            col.push(row.get::<_, f64>(k + 1).expect("value"));
        }
    }
    (t, v)
}

/// A vector magnitude per row.
fn magnitude(v: &[Vec<f64>]) -> Vec<f64> {
    (0..v[0].len()).map(|i| (v[0][i].powi(2) + v[1][i].powi(2) + v[2][i].powi(2)).sqrt()).collect()
}

/// The M4 envelope of [t0, t1] at `width` columns: indices of the first,
/// minimum, maximum and last sample of each column, in time order.
fn envelope(t: &[f64], v: &[f64], t0: f64, t1: f64, width: usize) -> Vec<usize> {
    let span = (t1 - t0) / width as f64;
    let mut out: Vec<usize> = Vec::new();
    let mut i = 0;
    while i < t.len() {
        let col = ((t[i] - t0) / span).floor();
        let (first, mut lo, mut hi, mut end) = (i, i, i, i);
        while end + 1 < t.len() && ((t[end + 1] - t0) / span).floor() == col {
            end += 1;
            if v[end] < v[lo] {
                lo = end;
            }
            if v[end] > v[hi] {
                hi = end;
            }
        }
        let mut picks = [first, lo, hi, end];
        picks.sort_unstable();
        for p in picks {
            if out.last() != Some(&p) {
                out.push(p);
            }
        }
        i = end + 1;
    }
    out
}

/// About `n` round tick values covering [lo, hi].
fn ticks(lo: f64, hi: f64, n: usize) -> Vec<f64> {
    let raw = (hi - lo) / n as f64;
    let mag = 10f64.powf(raw.log10().floor());
    let step = [1.0, 2.0, 2.5, 5.0, 10.0].iter().map(|m| m * mag).find(|s| *s >= raw).unwrap_or(mag * 10.0);
    let mut out = Vec::new();
    let mut x = (lo / step).ceil() * step;
    while x <= hi + step * 1e-9 {
        out.push(x);
        x += step;
    }
    out
}

/// Draws the plot on any plotters backend of `w` x `h` pixels.
fn draw<DB: DrawingBackend>(root: DrawingArea<DB, plotters::coord::Shift>, traces: &[Trace], w: i32, h: i32)
where
    DB::ErrorType: 'static,
{
    root.fill(&WHITE).unwrap();
    let scale = w as f64 / 1600.0; // sizes are given for a 1600-wide page
    let px = |x: f64| (x * scale) as i32;
    let font = |size: f64| ("sans-serif", size * scale).into_font();
    let (left, right, top, bottom) = (px(70.0) + px(95.0) * traces.len() as i32, w - px(40.0), px(80.0), h - px(110.0));
    let (t0, t1) = (-4.44, 93.32);
    let x_of = |t: f64| left + ((t - t0) / (t1 - t0) * (right - left) as f64) as i32;

    // Title.
    root.draw(&Text::new("imutag.db3", (w / 2, px(30.0)), TextStyle::from(font(22.0).style(FontStyle::Bold)).pos(Pos::new(HPos::Center, VPos::Center))))
        .unwrap();

    // The x axis: line, ticks, labels and grid.
    let grid = RGBColor(220, 220, 220);
    root.draw(&PathElement::new(vec![(left, bottom), (right, bottom)], BLACK.stroke_width(px(1.5) as u32))).unwrap();
    for t in ticks(t0, t1, 10) {
        let x = x_of(t);
        root.draw(&PathElement::new(vec![(x, top), (x, bottom)], grid.stroke_width(1))).unwrap();
        root.draw(&PathElement::new(vec![(x, bottom), (x, bottom + px(8.0))], BLACK.stroke_width(px(1.5) as u32))).unwrap();
        root.draw(&Text::new(format!("{t:.0}"), (x, bottom + px(22.0)), TextStyle::from(font(16.0)).pos(Pos::new(HPos::Center, VPos::Center))))
            .unwrap();
    }
    root.draw(&Text::new("Elapsed Time (s)", ((left + right) / 2, bottom + px(60.0)), TextStyle::from(font(17.0)).pos(Pos::new(HPos::Center, VPos::Center))))
        .unwrap();

    // One y axis per trace, the first outermost, each in its trace's colour,
    // and the trace drawn from its envelope.
    let columns = 3000; // print resolution: about one column per printer dot across the plot
    for (k, tr) in traces.iter().enumerate() {
        let (lo, hi) = tr.v.iter().fold((f64::MAX, f64::MIN), |(a, b), x| (a.min(*x), b.max(*x)));
        let pad = (hi - lo) * 0.05;
        let (lo, hi) = (lo - pad, hi + pad);
        let y_of = |v: f64| bottom - ((v - lo) / (hi - lo) * (bottom - top) as f64) as i32;
        let ax = left - px(95.0) * (traces.len() - k) as i32 + px(80.0);
        let color = tr.color;
        root.draw(&PathElement::new(vec![(ax, top), (ax, bottom)], color.stroke_width(px(1.5) as u32))).unwrap();
        for v in ticks(lo, hi, 8) {
            let y = y_of(v);
            if k == traces.len() - 1 {
                root.draw(&PathElement::new(vec![(left, y), (right, y)], grid.stroke_width(1))).unwrap();
            }
            root.draw(&PathElement::new(vec![(ax - px(7.0), y), (ax, y)], color.stroke_width(px(1.5) as u32))).unwrap();
            root.draw(&Text::new(format!("{v}"), (ax - px(11.0), y), font(14.0).color(&color).pos(Pos::new(HPos::Right, VPos::Center))))
                .unwrap();
        }
        let label_style = font(15.0).transform(FontTransform::Rotate270).color(&color).pos(Pos::new(HPos::Center, VPos::Center));
        root.draw(&Text::new(tr.label, (ax - px(72.0), (top + bottom) / 2), label_style)).unwrap();

        let picks = envelope(&tr.t, &tr.v, t0, t1, columns);
        let points: Vec<(i32, i32)> = picks.iter().map(|&i| (x_of(tr.t[i]), y_of(tr.v[i]))).collect();
        root.draw(&PathElement::new(points, color.stroke_width(px(1.2).max(1) as u32))).unwrap();
    }

    // The metadata box and the legend.
    let boxed = |x: i32, y: i32, rows: &[(String, Option<RGBColor>)]| {
        let (bw, bh) = (px(330.0), px(26.0) * rows.len() as i32 + px(14.0));
        root.draw(&Rectangle::new([(x, y), (x + bw, y + bh)], WHITE.filled())).unwrap();
        root.draw(&Rectangle::new([(x, y), (x + bw, y + bh)], RGBColor(150, 150, 150).stroke_width(1))).unwrap();
        for (i, (text, swatch)) in rows.iter().enumerate() {
            let y = y + px(20.0) + px(26.0) * i as i32;
            let mut tx = x + px(12.0);
            if let Some(c) = swatch {
                root.draw(&PathElement::new(vec![(tx, y), (tx + px(28.0), y)], c.stroke_width(px(3.0) as u32))).unwrap();
                tx += px(38.0);
            }
            root.draw(&Text::new(text.clone(), (tx, y), TextStyle::from(("monospace", 15.0 * scale).into_font()).pos(Pos::new(HPos::Left, VPos::Center))))
                .unwrap();
        }
    };
    boxed(left + px(20.0), top + px(16.0), &[("Start:  2026-08-27 12:35:46 UTC".into(), None), ("MSL:    1013.25 mbar".into(), None)]);
    let legend: Vec<(String, Option<RGBColor>)> = traces.iter().map(|t| (t.label.to_string(), Some(t.color))).collect();
    boxed(right - px(350.0), top + px(16.0), &legend);
    root.present().unwrap();
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let db = rusqlite::Connection::open(&args[1]).expect("open log");
    let (tg, g) = read(&db, "ImuGyro", &["gx", "gy", "gz"]);
    let (ta, a) = read(&db, "ImuAccel", &["ax", "ay", "az"]);
    let (tm, m) = read(&db, "ImuMag", &["mx", "my", "mz"]);
    let (tp, p) = read(&db, "ImuPressure", &["Pressure"]);
    let altitude = p[0].iter().map(|p| 44330.0 * (1.0 - (p / 1013.25).powf(1.0 / 5.255))).collect();
    let traces = [
        Trace { label: "Gyro magnitude dps", color: RGBColor(160, 82, 45), t: tg, v: magnitude(&g) },
        Trace { label: "Acceleration magnitude mg", color: RGBColor(43, 58, 103), t: ta, v: magnitude(&a) },
        Trace { label: "Magnetic field magnitude uT", color: RGBColor(85, 107, 47), t: tm, v: magnitude(&m) },
        Trace { label: "Altitude m", color: RGBColor(123, 79, 201), t: tp, v: altitude },
    ];
    let (w, h) = (1600, 1100);
    let svg = format!("{}.svg", args[2]);
    let png = format!("{}.png", args[2]);
    let start = std::time::Instant::now();
    draw(SVGBackend::new(&svg, (w, h)).into_drawing_area(), &traces, w as i32, h as i32);
    let svg_ms = start.elapsed().as_millis();
    let start = std::time::Instant::now();
    draw(BitMapBackend::new(&png, (w * 2, h * 2)).into_drawing_area(), &traces, w as i32 * 2, h as i32 * 2);
    println!("{svg} ({svg_ms} ms), {png} at 2x ({} ms)", start.elapsed().as_millis());
}
