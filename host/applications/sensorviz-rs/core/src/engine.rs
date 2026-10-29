//! The engine: the Qt sensorviz's `MainWindow` behaviour, with no UI.
//!
//! [`spawn`] starts the engine and returns an [`Engine`] handle. The UI talks
//! to it only through [`Engine::command`] and [`Engine::query`], and hears
//! from it only through the [`Event`] sink: the whole view state after every
//! change, and log lines.
//!
//! # Threading
//!
//! One thread, the **engine thread**, owns all state in one [`Viewer`]: the
//! open log's [`Session`], the per-tag [`Preferences`], the status line and
//! the outstanding message. Commands and queries are applied on it in the
//! order they arrive. [`Session`] is `Send` and not `Sync`, which this keeps.
//!
//! # Commands
//!
//! | Command | Arguments | Does |
//! | --- | --- | --- |
//! | `ready` | | Opens [`EngineConfig::initial_log`] and loads [`EngineConfig::initial_preferences`], if set |
//! | `open` | `path` | File > Load: remembers the open log's display choices, then opens the log and applies its tag's preferences |
//! | `set_visible_streams` | `ids` | View > Visible Streams |
//! | `set_stream_range` | `id`, `lower`, `upper` | View > Ranges: the range and the ranges linked to it |
//! | `reset_zoom` | | View > Reset Zoom: clears the custom ranges |
//! | `set_break_at_gaps` | `on` | View > Break Lines at Gaps |
//! | `set_cursors_visible` | `on` | View > Show Cursors |
//! | `set_utc_offset` | `hours` | Configuration > UTC Offset |
//! | `set_parameter` | `id`, `value` | A transform parameter (Sea-level Pressure, Declination, Battery Forward) |
//! | `set_title`, `set_title_visible` | `text`, `on` | The graph title |
//! | `set_colors`, `set_axis_sides` | `colors` / `sides`: id to `#rrggbb` / `left`/`right` | Stream Colors, Axis Sides |
//! | `load_preferences`, `store_preferences` | `path` | File > Preferences > Load, Store |
//! | `load_default_preferences` | | File > Preferences > Load Defaults |
//! | `save_file` | `path`, `base64` | Writes bytes the page made (File > Export PDF) |
//!
//! # Queries
//!
//! | Query | Arguments | Answer |
//! | --- | --- | --- |
//! | `envelope` | `t0`, `t1`, `width` | `{traces: {id: {t, v}}, ms}`: each drawn trace's M4 envelope at `width` columns; a gap marker's value is `null` |
//! | `hover` | `t` | `{time, rows: [{text}]}`: the hover readout |
//! | `range` | `id` | `{lower, upper}`: where the range dialog starts |
//! | `compass` | `t` | The compass panel at `t`, from the sample at or after it: `{time, heading, pitch, roll, dip, field, gravity, declination, batteryForward}`, angles as the Qt panel shows them; null without a compass |
//! | `about` | | `{title, version, userGuide, previewDialog}` |

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, Instant};

use base64::Engine as _;
use sensorviz_session::{OrientationSample, Preferences, Session, Side};
use serde_json::{Map, Value, json};

/// Where the engine starts, and what it tells Help.
#[derive(Debug, Clone, Default)]
pub struct EngineConfig {
    /// The user's home folder, where the file dialogs start before a log is open.
    pub home_dir: PathBuf,
    /// The user manual page for Help > User Guide, if one is installed.
    pub user_guide_url: Option<String>,
    /// A log to open on `ready`, as the Qt app's `--load-log` does.
    pub initial_log: Option<PathBuf>,
    /// A preference file to load on `ready`, as `--load-preferences` does.
    pub initial_preferences: Option<PathBuf>,
    /// A dialog for the page to open once the log is drawn (`visible`,
    /// `sides`, `colors`, `calibration`), for screenshots of the app.
    pub preview_dialog: Option<String>,
}

/// What the engine tells the UI, through the sink given to [`spawn`].
///
/// The sink is called on the engine thread for [`Event::State`], and on
/// whatever thread logged for [`Event::Log`], so it must be cheap and must not
/// call [`Engine::query`] and wait.
#[derive(Debug, Clone)]
pub enum Event {
    /// The whole view state, sent only when it differs from the last one.
    State(Value),
    /// One log line at or above INFO.
    Log {
        /// The level's name: `TRACE`, `DEBUG`, `INFO`, `WARN` or `ERROR`.
        level: &'static str,
        /// The message, without the level.
        text: String,
    },
}

/// The UI's side of the engine: a cheap, cloneable sender of messages.
///
/// Commands are fire-and-forget; their effect is seen in a later
/// [`Event::State`].
#[derive(Clone)]
pub struct Engine {
    /// The engine thread's message channel.
    tx: Sender<Msg>,
}

/// Messages to the engine thread, applied in the order they arrive.
#[derive(Debug)]
enum Msg {
    /// A UI command: its name and its JSON arguments.
    Command(String, Value),
    /// A UI query: its name, its JSON arguments and the channel for its answer.
    Query(String, Value, Sender<Value>),
}

impl Engine {
    /// Sends a command. Returns at once; an unknown command is logged as a
    /// warning, and a command sent after the engine has gone is dropped.
    pub fn command(&self, cmd: &str, args: Value) {
        let _ = self.tx.send(Msg::Command(cmd.to_owned(), args));
    }

    /// Asks a query and waits for its answer: [`Value::Null`] if the engine
    /// thread has gone or does not answer within 30 s (a large log can take
    /// that long to open ahead of it), and `{ "error": ... }` for an unknown
    /// query.
    pub fn query(&self, name: &str, args: Value) -> Value {
        let (reply_tx, reply_rx) = mpsc::channel();
        if self
            .tx
            .send(Msg::Query(name.to_owned(), args, reply_tx))
            .is_err()
        {
            return Value::Null;
        }
        reply_rx
            .recv_timeout(Duration::from_secs(30))
            .unwrap_or(Value::Null)
    }
}

/// The event sink, shared between the engine thread and the logger.
type Sink = Arc<dyn Fn(Event) + Send + Sync>;

/// The sink [`UiLogger`] forwards to, set by [`spawn`].
static SINK: OnceLock<Mutex<Option<Sink>>> = OnceLock::new();
/// The level passed on, as `log::Level as u8` (1 ERROR .. 5 TRACE).
static LOG_LEVEL: AtomicU8 = AtomicU8::new(3);

/// A `log` logger that forwards records to the sink as [`Event::Log`].
/// Install it once, before [`spawn`].
pub struct UiLogger;

/// The logger to install with `log::set_logger`.
pub static LOGGER: UiLogger = UiLogger;

impl log::Log for UiLogger {
    /// Passes records at or above INFO.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() as u8 <= LOG_LEVEL.load(Ordering::Relaxed)
    }

    /// Sends one enabled record to the sink.
    fn log(&self, record: &log::Record<'_>) {
        if !self.enabled(record.metadata()) {
            return;
        }
        let level = match record.level() {
            log::Level::Error => "ERROR",
            log::Level::Warn => "WARN",
            log::Level::Info => "INFO",
            log::Level::Debug => "DEBUG",
            log::Level::Trace => "TRACE",
        };
        if let Some(sink) = SINK.get().and_then(|s| s.lock().ok()?.clone()) {
            sink(Event::Log {
                level,
                text: record.args().to_string(),
            });
        }
    }

    /// Does nothing: nothing is buffered.
    fn flush(&self) {}
}

/// Starts the engine thread and returns its handle. Nothing is opened until
/// the page sends `ready`.
///
/// # Panics
///
/// Panics if the thread cannot be started, or the sensorViz library cannot
/// make a session (built from another version of its header, or out of
/// memory).
pub fn spawn(config: EngineConfig, sink: impl Fn(Event) + Send + Sync + 'static) -> Engine {
    let sink: Sink = Arc::new(sink);
    *SINK.get_or_init(|| Mutex::new(None)).lock().unwrap() = Some(sink.clone());
    log::set_max_level(log::LevelFilter::Info);
    let viewer = Viewer::new(config).expect("sensorviz session");
    let (tx, rx) = mpsc::channel();
    std::thread::Builder::new()
        .name("sensorviz-engine".into())
        .spawn(move || run(viewer, sink, rx))
        .expect("engine thread");
    Engine { tx }
}

/// The engine loop: apply messages and publish the view state when it changed.
fn run(mut viewer: Viewer, sink: Sink, rx: Receiver<Msg>) {
    let mut last_state = Value::Null;
    while let Ok(msg) = rx.recv() {
        match msg {
            Msg::Command(cmd, args) => viewer.command(&cmd, &args),
            Msg::Query(name, args, reply) => {
                let _ = reply.send(viewer.query(&name, &args));
            }
        }
        let state = viewer.view_state();
        if state != last_state {
            sink(Event::State(state.clone()));
            last_state = state;
        }
    }
}

/// A message for the page to show once: a failed load, a refused file.
#[derive(Debug, Clone, PartialEq)]
struct Message {
    /// Increases with each message, so the page shows each once.
    seq: u64,
    /// Dialog title, e.g. `Load Failed`.
    title: String,
    /// The message.
    text: String,
}

/// Everything the engine thread owns.
pub struct Viewer {
    /// Where the engine starts and what Help says.
    config: EngineConfig,
    /// The open log, its view state and transforms.
    session: Session,
    /// Per-tag display preferences; outlive logs.
    preferences: Preferences,
    /// The last message for the page, if any.
    message: Option<Message>,
    /// The directory of the last file opened or saved, where dialogs start.
    last_dir: PathBuf,
    /// The open log's compass samples, for the compass panel; empty without one.
    compass: Vec<OrientationSample>,
}

/// A string argument, or "".
fn arg_str<'a>(args: &'a Value, key: &str) -> &'a str {
    args.get(key).and_then(Value::as_str).unwrap_or("")
}

/// A number argument, if it is one.
fn arg_f64(args: &Value, key: &str) -> Option<f64> {
    args.get(key).and_then(Value::as_f64)
}

/// A boolean argument, or false.
fn arg_bool(args: &Value, key: &str) -> bool {
    args.get(key).and_then(Value::as_bool).unwrap_or(false)
}

/// `#rrggbb` as 0xRRGGBB.
fn parse_color(text: &str) -> Option<u32> {
    let hex = text.strip_prefix('#')?;
    if hex.len() != 6 {
        return None;
    }
    u32::from_str_radix(hex, 16).ok()
}

impl Viewer {
    /// A viewer with no log open.
    ///
    /// # Errors
    ///
    /// The library's error when it cannot make a session or a preference
    /// store.
    pub fn new(config: EngineConfig) -> Result<Self, sensorviz_session::Error> {
        Ok(Viewer {
            last_dir: config.home_dir.clone(),
            session: Session::new()?,
            preferences: Preferences::new()?,
            message: None,
            compass: Vec::new(),
            config,
        })
    }

    /// Queues a message for the page.
    fn tell(&mut self, title: &str, text: impl Into<String>) {
        let seq = self.message.as_ref().map_or(1, |m| m.seq + 1);
        self.message = Some(Message {
            seq,
            title: title.into(),
            text: text.into(),
        });
    }

    /// Remembers `path`'s directory for the next dialog.
    fn remember_dir(&mut self, path: &Path) {
        if let Some(dir) = path.parent() {
            self.last_dir = dir.to_path_buf();
        }
    }

    /// Whether a log is open.
    fn has_log(&mut self) -> bool {
        self.session
            .catalog()
            .map(|c| !c.path.is_empty())
            .unwrap_or(false)
    }

    /// Applies one command; see the module's table.
    pub fn command(&mut self, cmd: &str, args: &Value) {
        match cmd {
            "ready" => {
                if let Some(path) = self.config.initial_log.clone() {
                    self.open(&path);
                }
                if let Some(path) = self.config.initial_preferences.clone() {
                    self.load_preferences(&path);
                }
            }
            "open" => self.open(Path::new(arg_str(args, "path"))),
            "set_visible_streams" => {
                let ids: Vec<&str> = args
                    .get("ids")
                    .and_then(Value::as_array)
                    .map(|a| a.iter().filter_map(Value::as_str).collect())
                    .unwrap_or_default();
                let _ = self.session.set_visible_streams(&ids);
            }
            "set_stream_visible" => {
                self.session
                    .set_stream_visible(arg_str(args, "id"), arg_bool(args, "visible"));
            }
            "set_stream_range" => {
                let (Some(lower), Some(upper)) = (arg_f64(args, "lower"), arg_f64(args, "upper"))
                else {
                    return;
                };
                if lower >= upper {
                    self.tell("Set Range", "Minimum must be less than maximum.");
                    return;
                }
                self.session
                    .set_stream_range(arg_str(args, "id"), lower, upper);
            }
            "reset_zoom" => {
                self.session.reset_zoom();
            }
            "set_break_at_gaps" => {
                self.session.set_break_at_gaps(arg_bool(args, "on"));
            }
            "set_cursors_visible" => {
                self.session.set_cursors_visible(arg_bool(args, "on"));
            }
            "set_utc_offset" => {
                if let Some(hours) = arg_f64(args, "hours") {
                    self.session
                        .set_utc_offset(hours.round().clamp(-12.0, 12.0) as i32);
                }
            }
            "set_parameter" => {
                if let Some(value) = arg_f64(args, "value")
                    && let Err(e) = self.session.set_parameter(arg_str(args, "id"), value)
                {
                    log::warn!("{}: {e}", arg_str(args, "id"));
                }
            }
            "set_title" => {
                let _ = self.session.set_title(arg_str(args, "text"));
            }
            "set_title_visible" => {
                self.session.set_title_visible(arg_bool(args, "on"));
            }
            "set_colors" => {
                let colors: Vec<(String, u32)> = args
                    .get("colors")
                    .and_then(Value::as_object)
                    .map(|m| {
                        m.iter()
                            .filter_map(|(id, c)| Some((id.clone(), parse_color(c.as_str()?)?)))
                            .collect()
                    })
                    .unwrap_or_default();
                let refs: Vec<(&str, u32)> =
                    colors.iter().map(|(id, c)| (id.as_str(), *c)).collect();
                let _ = self.session.set_colors(&refs);
            }
            "set_axis_sides" => {
                let sides: Vec<(String, Side)> = args
                    .get("sides")
                    .and_then(Value::as_object)
                    .map(|m| {
                        m.iter()
                            .map(|(id, s)| {
                                let side = if s.as_str() == Some("right") {
                                    Side::Right
                                } else {
                                    Side::Left
                                };
                                (id.clone(), side)
                            })
                            .collect()
                    })
                    .unwrap_or_default();
                let refs: Vec<(&str, Side)> =
                    sides.iter().map(|(id, s)| (id.as_str(), *s)).collect();
                let _ = self.session.set_axis_sides(&refs);
            }
            "load_preferences" => self.load_preferences(Path::new(arg_str(args, "path"))),
            "store_preferences" => self.store_preferences(Path::new(arg_str(args, "path"))),
            "load_default_preferences" => {
                if self.has_log() {
                    self.session.forget_preferences(&mut self.preferences);
                    self.session.apply_preferences(&self.preferences);
                }
            }
            "save_file" => {
                let path = PathBuf::from(arg_str(args, "path"));
                let written = base64::engine::general_purpose::STANDARD
                    .decode(arg_str(args, "base64"))
                    .map_err(|e| e.to_string())
                    .and_then(|bytes| std::fs::write(&path, bytes).map_err(|e| e.to_string()));
                match written {
                    Ok(()) => {
                        self.remember_dir(&path);
                        log::info!("Saved {}", path.display());
                    }
                    Err(e) => self.tell(
                        "Save Failed",
                        format!("Could not write {}: {e}", path.display()),
                    ),
                }
            }
            other => log::warn!("unknown command {other}"),
        }
    }

    /// File > Load: remembers the open log's display choices for its tag,
    /// then opens `path` and applies its tag's preferences.
    fn open(&mut self, path: &Path) {
        if self.has_log() {
            self.session.remember_preferences(&mut self.preferences);
        }
        let started = Instant::now();
        match self.session.open(path, Some(&self.preferences)) {
            Ok(()) => {
                self.remember_dir(path);
                self.compass = self.session.compass_samples();
                log::info!(
                    "Loaded {} in {} ms",
                    path.display(),
                    started.elapsed().as_millis()
                );
                if let Ok(catalog) = self.session.catalog() {
                    for failure in catalog.transform_failures {
                        if failure.transform == "compass" {
                            self.tell("Compass Streams", failure.error);
                        } else {
                            log::warn!("{}: {}", failure.transform, failure.error);
                        }
                    }
                }
            }
            Err(e) => self.tell("Load Failed", e.to_string()),
        }
    }

    /// File > Preferences > Load: replaces every entry from the file, then
    /// applies the open log's.
    fn load_preferences(&mut self, path: &Path) {
        let loaded = std::fs::read_to_string(path)
            .map_err(|e| format!("Could not open {}: {e}", path.display()))
            .and_then(|text| {
                self.preferences
                    .load(&text, &path.to_string_lossy())
                    .map_err(|e| e.to_string())
            });
        match loaded {
            Ok(()) => {
                self.remember_dir(path);
                if self.has_log() {
                    self.session.apply_preferences(&self.preferences);
                }
                log::info!("Loaded sensorViz preferences from {}", path.display());
            }
            Err(e) => self.tell("Load Preferences Failed", e),
        }
    }

    /// File > Preferences > Store: remembers the open log's choices, then
    /// writes every entry.
    fn store_preferences(&mut self, path: &Path) {
        if self.has_log() {
            self.session.remember_preferences(&mut self.preferences);
        }
        let text = self.preferences.save();
        match std::fs::write(path, text) {
            Ok(()) => {
                self.remember_dir(path);
                log::info!("Saved sensorViz preferences to {}", path.display());
            }
            Err(e) => self.tell(
                "Save Preferences Failed",
                format!("Could not write {}: {e}", path.display()),
            ),
        }
    }

    /// Answers one query; see the module's table.
    pub fn query(&mut self, name: &str, args: &Value) -> Value {
        match name {
            "envelope" => self.envelope(args),
            "hover" => {
                let t = arg_f64(args, "t").unwrap_or(0.0);
                let rows = self.session.hover(t).unwrap_or_default();
                let mut rows = rows.into_iter();
                let time = rows.next().unwrap_or_default();
                json!({ "time": time, "rows": rows.map(|text| json!({ "text": text })).collect::<Vec<_>>() })
            }
            "range" => match self.session.range_for_dialog(arg_str(args, "id")) {
                Some((lower, upper)) => json!({ "lower": lower, "upper": upper }),
                None => Value::Null,
            },
            "compass" => self.compass_at(arg_f64(args, "t")),
            "about" => json!({
                "title": "sensorViz",
                "version": env!("CARGO_PKG_VERSION"),
                "userGuide": self.config.user_guide_url,
                "previewDialog": self.config.preview_dialog,
            }),
            other => json!({ "error": format!("unknown query {other}") }),
        }
    }

    /// The compass panel at `t` (the first sample if none is given), as the
    /// Qt app's MyCompass.qml shows it: from the sample at or after `t` (the
    /// last after the end), its display angles (the core's), the
    /// declination added to the heading, and with the battery aft the
    /// heading turned 180 degrees and pitch and roll negated.
    fn compass_at(&mut self, t: Option<f64>) -> Value {
        if self.compass.is_empty() {
            return Value::Null;
        }
        let i = t
            .map_or(0, |t| self.compass.partition_point(|s| s.time < t))
            .min(self.compass.len() - 1);
        let sample = self.compass[i];
        let angles = calibsession::display_angles(sample.q);
        let parameters = self
            .session
            .view()
            .map(|v| v.parameters)
            .unwrap_or_default();
        let declination = parameters
            .get("compass.declination_deg")
            .copied()
            .unwrap_or(0.0);
        let forward = parameters
            .get("compass.battery_forward")
            .copied()
            .unwrap_or(1.0)
            != 0.0;
        let (mut heading, mut pitch, mut roll) =
            (angles.yaw + declination, angles.pitch, angles.roll);
        if !forward {
            heading += 180.0;
            pitch = -pitch;
            roll = -roll;
        }
        heading = (heading + 360.0) % 360.0;
        json!({
            "time": sample.time,
            "heading": heading, "pitch": pitch, "roll": roll,
            "dip": sample.dip, "field": sample.field, "gravity": sample.mg,
            "declination": declination, "batteryForward": forward,
        })
    }

    /// Every drawn trace's envelope over `[t0, t1]` at `width` columns.
    fn envelope(&mut self, args: &Value) -> Value {
        let started = Instant::now();
        let Ok(spec) = self.session.spec() else {
            return json!({ "traces": {}, "ms": 0 });
        };
        let t0 = arg_f64(args, "t0").unwrap_or(f64::NEG_INFINITY);
        let t1 = arg_f64(args, "t1").unwrap_or(f64::INFINITY);
        let width = arg_f64(args, "width").unwrap_or(1.0).round().max(1.0) as i32;
        let mut traces = Map::new();
        for trace in &spec.traces {
            let envelope = self
                .session
                .envelope(trace.stream, t0, t1, width, spec.break_at_gaps);
            traces.insert(
                trace.id.clone(),
                json!({ "t": envelope.time, "v": envelope.value }),
            );
        }
        json!({ "traces": traces, "ms": started.elapsed().as_secs_f64() * 1000.0 })
    }

    /// The whole view state the page draws from.
    pub fn view_state(&mut self) -> Value {
        let [catalog, view, spec] = self.session.state_json();
        let catalog: Value = serde_json::from_str(&catalog).unwrap_or(Value::Null);
        let view: Value = serde_json::from_str(&view).unwrap_or(Value::Null);
        let has_log = catalog["path"].as_str().is_some_and(|p| !p.is_empty());
        let parameters = if has_log {
            self.session
                .parameters()
                .map(|ps| {
                    ps.into_iter()
                        .map(|p| {
                            json!({
                                "id": p.id, "title": p.title, "prompt": p.prompt, "units": p.units,
                                "kind": p.kind, "minimum": p.minimum, "maximum": p.maximum,
                                "decimals": p.decimals, "value": p.value,
                            })
                        })
                        .collect()
                })
                .unwrap_or_default()
        } else {
            Vec::new()
        };
        let path = catalog["path"].as_str().unwrap_or("").to_owned();
        json!({
            "title": "sensorViz",
            "hasLog": has_log,
            "path": path,
            "fileName": Path::new(&path).file_name().map(|n| n.to_string_lossy().into_owned()),
            "dialogDir": self.last_dir,
            "status": status_line(&catalog, &view, has_log),
            "fileInfo": if has_log { file_info(&catalog) } else { String::new() },
            "timeDomain": catalog["timeDomain"],
            "hasCompass": catalog["compassSource"].as_str().is_some_and(|s| !s.is_empty()),
            "hasCalibration": catalog["compassCalibration"]["present"],
            "calibration": catalog["compassCalibration"],
            "spec": if has_log { serde_json::from_str(&spec).unwrap_or(Value::Null) } else { Value::Null },
            "view": if has_log { view } else { Value::Null },
            "parameters": parameters,
            "message": self.message.as_ref().map(|m| json!({ "seq": m.seq, "title": m.title, "text": m.text })),
        })
    }
}

/// The status line, as the Qt app's `updateMetadata()` writes it.
fn status_line(catalog: &Value, view: &Value, has_log: bool) -> String {
    if !has_log {
        return "No log loaded".into();
    }
    let streams = view["streams"].as_array().map_or(0, Vec::len);
    let sets = catalog["recordSets"].as_array().map_or(0, Vec::len);
    if sets == 0 {
        format!("{streams} streams loaded")
    } else {
        format!("{streams} streams, {sets} record sets loaded")
    }
}

/// The File Info pane, line for line as the Qt app's `updateMetadata()`
/// writes it: the file, tag type, profile, record sets, the compass
/// calibration, the compass source, then the info rows but for the large
/// `config` and `info` blobs.
fn file_info(catalog: &Value) -> String {
    let text = |v: &Value| v.as_str().unwrap_or("").to_owned();
    let mut lines = Vec::new();
    let path = text(&catalog["path"]);
    lines.push(
        Path::new(&path)
            .file_name()
            .map_or(path.clone(), |n| n.to_string_lossy().into_owned()),
    );
    let tag_type = text(&catalog["tagType"]);
    if !tag_type.is_empty() {
        lines.push(format!("Tag type: {tag_type}"));
    }
    let profile = text(&catalog["profileName"]);
    if !profile.is_empty() {
        lines.push(format!("Profile: {profile}"));
    }
    if let Some(sets) = catalog["recordSets"].as_array().filter(|s| !s.is_empty()) {
        let labels: Vec<String> = sets.iter().map(|s| text(&s["label"])).collect();
        lines.push(format!("Record sets: {}", labels.join(", ")));
    }
    let calibration = &catalog["compassCalibration"];
    let warning = text(&calibration["warning"]);
    if calibration["present"].as_bool() == Some(true) {
        lines.push("Compass calibration: loaded".into());
        lines.push(format!(
            "Compass calibration epoch: {}",
            calibration["epoch"]
        ));
        if !warning.is_empty() {
            lines.push(format!("Compass calibration warning: {warning}"));
        }
    } else if !warning.is_empty() {
        lines.push(format!("Compass calibration: {warning}"));
    }
    let source = text(&catalog["compassSource"]);
    if !source.is_empty() {
        lines.push(format!("Compass orientation: {source}"));
    }
    if let Some(info) = catalog["info"].as_object() {
        for (field, value) in info {
            if field == "config" || field == "info" {
                continue;
            }
            lines.push(format!("{field}: {}", value.as_str().unwrap_or("")));
        }
    }
    lines.join("\n")
}
