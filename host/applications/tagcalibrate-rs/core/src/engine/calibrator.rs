//! The engine thread's state and rules: qtcalibrate's `MainWindow`, without
//! its widgets.
//!
//! [`Calibrator`] owns the calibration session, the sample capture, the
//! display settings, the prompt on screen and the timers. Each UI command,
//! tag report and timer changes it; `view.rs` turns it into the view state.
//! Every behaviour names the qtcalibrate method it follows.

use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::time::{Duration, Instant};

use calibsession::{Orientation, Session, SessionError};
use log::{debug, info, warn};
use serde_json::{Value, json};
use tagcore::UsbDev;
use tagcore::proto::{CalibrationConstants, TagInfo, calibration_constants};

use super::capture::{self, Capture, CaptureSample, Replay};
use super::tagthread::{Report, Sample, TagCmd};
use super::{EngineConfig, query_error, set_log_level};

/// How often the quality figures are recomputed while collecting from a tag:
/// qtcalibrate's 200 ms quality timer, one update every two samples.
const QUALITY_PERIOD: Duration = Duration::from_millis(200);

/// In a replay, the quality update runs every this many samples instead, so
/// the result depends on the capture alone (qtcalibrate's
/// `kReplayQualityInterval`).
const REPLAY_QUALITY_INTERVAL: usize = 2;

/// How long Quit waits for the tag thread to stop the stream and detach
/// before exiting anyway: [`STOP_SETTLE`](super::tagthread) and the detach,
/// with room to spare.
const QUIT_WAIT: Duration = Duration::from_secs(5);

/// A dialog the engine is waiting on.
#[derive(Debug)]
pub(crate) enum PromptKind {
    /// A message with an OK button.
    Alert {
        /// Window title.
        title: String,
        /// The message.
        text: String,
        /// "warning" or "info".
        style: &'static str,
    },
    /// Choose one of several bases.
    ChooseBase(Vec<(UsbDev, Option<String>)>),
    /// Where to save the sample capture.
    SaveCapture,
}

/// The prompt on screen, by id.
#[derive(Debug)]
pub(crate) struct Prompt {
    /// Increases with each prompt; the page answers by it.
    pub id: u64,
    /// What it asks.
    pub kind: PromptKind,
}

/// The live compass, as the last sample with an accelerometer gave it.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Live {
    /// The solved orientation.
    pub orientation: Orientation,
    /// The raw acceleration magnitude, mg (qtcalibrate shows the unfiltered
    /// reading as Gravity).
    pub gravity: f32,
}

/// The engine thread's state.
pub(crate) struct Calibrator {
    /// Paths and options.
    pub config: EngineConfig,
    /// The tag thread's command channel.
    tag_tx: Sender<TagCmd>,
    /// The calibration (the C++ session).
    pub session: Session,
    /// Attached to a tag, or replaying a capture.
    pub attached: bool,
    /// Between Attach and the tag thread's answer.
    pub attaching: bool,
    /// The attached tag's identity.
    pub info: Option<TagInfo>,
    /// Streaming samples (qtcalibrate's Stream check box).
    pub streaming: bool,
    /// Collecting calibration samples (between Start and Stop).
    pub collecting: bool,
    /// The capture Start records.
    pub capture: Capture,
    /// Calibration-log reads since the capture began: each sample's
    /// `batch_index` (qtcalibrate's `captureBatchIndex`).
    capture_batch: i32,
    /// The replay, when there is one.
    pub replay: Option<Replay>,
    /// When the next replayed sample is due.
    replay_due: Option<Instant>,
    /// When the next quality update is due, while collecting from a tag.
    quality_due: Option<Instant>,
    /// Whether any quality update has run since the last clear, so the
    /// figures read "--" before the first.
    pub quality_shown: bool,
    /// The live compass, while streaming.
    pub live: Option<Live>,
    /// The newest collected sample as plotted: calibrated once a calibration
    /// exists, raw before.
    pub newest: Option<[f32; 3]>,
    /// Declination added to the heading, degrees east.
    pub declination: f64,
    /// Whether the tag is shown battery forward.
    pub battery_forward: bool,
    /// The prompt on screen.
    pub prompt: Option<Prompt>,
    /// The id the next prompt gets.
    next_prompt: u64,
    /// Set when Quit is confirmed.
    exit: bool,
    /// While Quit waits for the tag thread to detach: when to exit anyway.
    quit_due: Option<Instant>,
}

impl Calibrator {
    /// A calibrator with an empty session.
    ///
    /// # Errors
    ///
    /// When the calibration library cannot make a session.
    pub fn new(config: EngineConfig, tag_tx: Sender<TagCmd>) -> Result<Calibrator, SessionError> {
        Ok(Calibrator {
            config,
            tag_tx,
            session: Session::new()?,
            attached: false,
            attaching: false,
            info: None,
            streaming: false,
            collecting: false,
            capture: Capture::default(),
            capture_batch: 0,
            replay: None,
            replay_due: None,
            quality_due: None,
            quality_shown: false,
            live: None,
            newest: None,
            declination: 0.0,
            battery_forward: true,
            prompt: None,
            next_prompt: 1,
            exit: false,
            quit_due: None,
        })
    }

    /// Sends a command to the tag thread.
    fn send(&self, cmd: TagCmd) {
        let _ = self.tag_tx.send(cmd);
    }

    /// Shows a prompt, replacing any other.
    fn ask(&mut self, kind: PromptKind) {
        self.prompt = Some(Prompt {
            id: self.next_prompt,
            kind,
        });
        self.next_prompt += 1;
    }

    /// Shows an alert.
    fn alert(&mut self, title: &str, text: &str, style: &'static str) {
        self.ask(PromptKind::Alert {
            title: title.into(),
            text: text.into(),
            style,
        });
    }

    /// Whether the window is replaying a capture rather than talking to a tag.
    pub fn replaying(&self) -> bool {
        self.replay.is_some()
    }

    /// Takes the exit request, once.
    pub fn take_exit(&mut self) -> bool {
        std::mem::take(&mut self.exit)
    }

    /// The earliest timer due.
    pub fn next_deadline(&self) -> Option<Instant> {
        [self.replay_due, self.quality_due, self.quit_due]
            .into_iter()
            .flatten()
            .min()
    }

    // ---- Commands -----------------------------------------------------------

    /// Applies one UI command.
    pub fn command(&mut self, cmd: &str, args: &Value) {
        match cmd {
            "ready" => self.ready(),
            "attach" => self.attach(),
            "detach" => self.detach(),
            "set_stream" => self.set_stream(args["on"].as_bool().unwrap_or(false)),
            "start" => self.start(),
            "stop" => self.stop(),
            "clear" => self.clear(),
            "tag_write" => self.tag_write(),
            "tag_read" => {
                if self.attached && !self.streaming && !self.replaying() {
                    self.send(TagCmd::ReadCalibration);
                }
            }
            "save_capture" => {
                if self.capture.ready() {
                    self.ask(PromptKind::SaveCapture);
                }
            }
            "set_declination" => {
                if let Some(d) = args["degrees"].as_f64().filter(|d| d.is_finite()) {
                    self.declination = d;
                }
            }
            "set_battery_forward" => self.battery_forward = args["on"].as_bool().unwrap_or(true),
            "set_log_level" => set_log_level(args["level"].as_str().unwrap_or("INFO")),
            "save_text" => {
                if let (Some(path), Some(text)) = (args["path"].as_str(), args["text"].as_str())
                    && let Err(e) = std::fs::write(path, text)
                {
                    self.alert("Error", &format!("Cannot open file: {e}"), "warning");
                }
            }
            "answer" => self.answer(args),
            "quit" => {
                // Exit only once the tag thread has stopped the stream and
                // detached: exiting at once left the tag calibrating, and a
                // calibrating tag refuses everything but Stop, through the
                // reset of the next attach.
                let tag_attached = self.attached && !self.replaying();
                self.detach();
                if tag_attached {
                    self.quit_due = Some(Instant::now() + QUIT_WAIT);
                } else {
                    self.exit = true;
                }
            }
            other => warn!("Unknown command {other}"),
        }
    }

    /// The page is up: start the replay, or attach as qtcalibrate does on
    /// opening.
    fn ready(&mut self) {
        if let Some(path) = self.config.replay.clone() {
            self.start_replay(&path);
        } else if self.config.auto_attach {
            self.attach();
        }
    }

    /// Loads a capture and looks attached to it (qtcalibrate's
    /// `setupReplayTag`).
    fn start_replay(&mut self, path: &Path) {
        match capture::load(path) {
            Ok(replay) => {
                info!(
                    "Loaded replay capture {} with {} samples",
                    path.display(),
                    replay.samples.len()
                );
                self.replay = Some(replay);
                self.attached = true;
                self.clear();
                info!("Using replay capture instead of USB tag");
            }
            Err(e) => self.alert("Replay", &e, "warning"),
        }
    }

    /// Attach: find the bases, choosing when there are several.
    fn attach(&mut self) {
        if self.attached || self.attaching {
            return;
        }
        self.attaching = true;
        self.send(TagCmd::FindBases);
    }

    /// Detach (qtcalibrate's `Detach`): ends a capture and the stream.
    fn detach(&mut self) {
        self.capture.finish();
        self.collecting = false;
        self.streaming = false;
        self.quality_due = None;
        self.replay_due = None;
        self.live = None;
        if self.replaying() {
            self.replay = None;
            self.attached = false;
        } else if self.attached {
            self.send(TagCmd::Detach);
        }
    }

    /// The Stream check box (`on_streamCheckBox_toggled`). Off acts as Stop
    /// first, so a capture is not lost by ending the stream.
    fn set_stream(&mut self, on: bool) {
        if !self.attached || on == self.streaming {
            return;
        }
        if on {
            self.streaming = true;
            if let Some(replay) = self.replay.as_mut() {
                if replay.cursor >= replay.samples.len() {
                    replay.cursor = 0;
                }
                self.replay_due = Some(Instant::now() + self.config.replay_period);
            } else {
                self.send(TagCmd::Stream(true));
            }
        } else {
            if self.collecting || self.capture.active {
                self.collecting = false;
                self.capture.finish();
            }
            self.streaming = false;
            self.quality_due = None;
            self.replay_due = None;
            self.live = None;
            if !self.replaying() {
                self.send(TagCmd::Stream(false));
            }
        }
    }

    /// Start (`on_startButton_clicked`): begins the capture and collection.
    fn start(&mut self) {
        if !self.streaming || self.collecting {
            return;
        }
        self.capture.begin();
        self.capture_batch = 0;
        info!("Calibration sample capture started");
        self.collecting = true;
        if !self.replaying() {
            self.quality_due = Some(Instant::now() + QUALITY_PERIOD);
        }
    }

    /// Stop (`on_stopButton_clicked`).
    fn stop(&mut self) {
        if !self.collecting {
            return;
        }
        self.collecting = false;
        self.quality_due = None;
        self.capture.finish();
    }

    /// Clear (`on_clearButton_clicked`): the session, the figures and the
    /// capture.
    fn clear(&mut self) {
        self.session.clear();
        self.quality_shown = false;
        self.newest = None;
        self.capture = Capture::default();
    }

    /// Tag Write (`on_saveButton_clicked`): the magnetometer constants, and
    /// the accelerometer offset beside them -- zero, meaning no correction,
    /// when there is no fit.
    fn tag_write(&mut self) {
        if !self.attached || self.streaming || self.replaying() {
            return;
        }
        let c = self.session.constants();
        if !c.valid {
            info!("save failed");
            return;
        }
        let [v0, v1, v2] = c.offset;
        let [[a00, a01, a02], [a10, a11, a12], [a20, a21, a22]] = c.mapping;
        let fit = self.session.accel_fit();
        let [o0, o1, o2] = if fit.valid {
            fit.offset
        } else {
            info!("No accelerometer fit: saving a zero accelerometer offset");
            [0.0; 3]
        };
        let constants = CalibrationConstants {
            timestamp: (capture::now_ms() / 1000) as i32,
            magnetometer: Some(calibration_constants::MagConstants {
                v0,
                v1,
                v2,
                a00,
                a01,
                a02,
                a10,
                a11,
                a12,
                a20,
                a21,
                a22,
            }),
            accelerometer: Some(calibration_constants::AccelConstants { o0, o1, o2 }),
        };
        self.send(TagCmd::WriteCalibration(Box::new(constants)));
    }

    /// Installs constants read from a tag (`on_loadButton_clicked`).
    fn load_constants(&mut self, c: &CalibrationConstants) {
        let Some(m) = &c.magnetometer else { return };
        self.session.set_constants(
            [m.v0, m.v1, m.v2],
            [
                [m.a00, m.a01, m.a02],
                [m.a10, m.a11, m.a12],
                [m.a20, m.a21, m.a22],
            ],
        );
        if let Some(a) = &c.accelerometer {
            self.session.set_accel_offset([a.o0, a.o1, a.o2]);
        }
        info!("Read timestamp {}", c.timestamp);
    }

    /// Answers the prompt with the page's id.
    fn answer(&mut self, args: &Value) {
        let Some(prompt) = self.prompt.take_if(|p| Some(p.id) == args["id"].as_u64()) else {
            return;
        };
        let ok = args["button"] == "ok";
        match prompt.kind {
            PromptKind::Alert { .. } => {}
            PromptKind::ChooseBase(bases) => {
                match args["index"]
                    .as_u64()
                    .and_then(|i| bases.get(i as usize))
                    .filter(|_| ok)
                {
                    Some((base, _)) => self.send(TagCmd::Attach(base.clone())),
                    None => self.attaching = false,
                }
            }
            PromptKind::SaveCapture => {
                if let (true, Some(path)) = (ok, args["path"].as_str()) {
                    self.save_capture(path);
                }
            }
        }
    }

    /// Writes the capture (`saveSampleCapture`); `.json` is added to a name
    /// without an extension.
    fn save_capture(&mut self, path: &str) {
        let mut path = PathBuf::from(path);
        if path.extension().is_none() {
            path.set_extension("json");
        }
        let tag = match &self.replay {
            Some(r) => r.tag.clone(),
            None => self.info.as_ref().map(capture::tag_json),
        };
        let calibration = self.calibration_json();
        let doc = capture::document(&self.capture, tag.as_ref(), calibration);
        match capture::write(&path, &doc) {
            Ok(()) => info!("Saved calibration sample capture {}", path.display()),
            Err(e) => self.alert(
                "Error",
                &format!("Cannot save sample capture file: {e}"),
                "warning",
            ),
        }
    }

    /// The capture's "calibration" block: the constants, and after a quality
    /// update the accelerometer fit and the quality figures, as qtcalibrate
    /// records them.
    fn calibration_json(&mut self) -> Value {
        let c = self.session.constants();
        if !c.valid {
            return json!({ "valid": false });
        }
        self.session.update_quality();
        let num = capture::num;
        let mut block = json!({
            "valid": true,
            "field": num(c.field),
            "offset": c.offset.map(num),
            "mapping": c.mapping.map(|r| r.map(num)),
        });
        let fit = self.session.accel_fit();
        if fit.valid {
            block["accelerometer"] = json!({
                "offset": fit.offset.map(num),
                "radius": num(fit.radius),
                "residual": num(fit.residual),
                "samples": fit.samples,
                "patches_seen": fit.patches_seen,
                "patches": fit.patches,
            });
        }
        let q = self.session.quality();
        let mut quality = json!({
            "field": num(q.field),
            "fit_error_percent": num(q.fit_error),
            "residual_spread_percent": num(q.residual_spread),
            "residual_p95_percent": num(q.residual_p95),
            "residual_hard_iron": num(q.residual_hard_iron),
            "patches_seen": q.patches_seen,
            "patches": q.mag_patches,
            "isotropy": num(q.isotropy),
        });
        if q.have_accel_metrics {
            quality["dip_degrees"] = num(q.dip_mean_deg);
            quality["dip_spread_degrees"] = num(q.dip_spread_deg);
        }
        block["quality"] = quality;
        block
    }

    // ---- Samples, timers and reports ----------------------------------------

    /// One calibration sample (`processCalibrationSample`): the live compass
    /// from it, then, while collecting, the capture and the session.
    fn sample(&mut self, batch_index: i32, sample_index: i32, sample: Sample) {
        let (mag, accel) = sample;
        if let Some(a) = accel
            && let Some(orientation) = self.session.orientation(mag, a)
        {
            let gravity = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt();
            self.live = Some(Live {
                orientation,
                gravity,
            });
        }
        if self.collecting {
            if self.capture.active {
                self.capture.samples.push(CaptureSample {
                    batch_index,
                    sample_index,
                    sample,
                });
            }
            if self.session.add_sample(mag, accel) {
                debug!("new calibration, B={:.2} uT", self.session.field());
            }
            self.newest = Some(if self.session.has_calibration() {
                self.session.calibrate(mag)
            } else {
                mag
            });
        }
    }

    /// The quality update (`TriggerQualityUpdate`), with qtcalibrate's four
    /// `DEBUG` lines in its format.
    fn quality_update(&mut self) {
        self.session.update_quality();
        self.quality_shown = true;
        let m = self.session.quality();
        if m.have_accel_metrics {
            debug!(
                "magquality: coverage {}/{} isotropy {:.3} attitude {:.2} ({} cells over {} patches) \
                 dip {:.2} +/- {:.2} deg (p95 {:.2}, {} of {} in gate)",
                m.patches_seen,
                m.mag_patches,
                m.isotropy,
                m.attitude_diversity,
                m.attitude_cells,
                m.attitude_patches,
                m.dip_mean_deg,
                m.dip_spread_deg,
                m.dip_p95_deg,
                m.dip_samples,
                m.accel_samples
            );
        } else {
            debug!(
                "magquality: coverage {}/{} isotropy {:.3} (no accelerometer)",
                m.patches_seen, m.mag_patches, m.isotropy
            );
        }
        debug!(
            "residual: field {:.2} uT fit {:.2}% spread {:.2}% p95 {:.2}% hard iron {:.2} uT ({} samples)",
            m.field,
            m.fit_error,
            m.residual_spread,
            m.residual_p95,
            m.residual_hard_iron,
            m.samples
        );
        let g = self.session.accel_fit();
        if g.valid {
            let [x, y, z] = g.offset;
            debug!(
                "gravity: offset {x:+.2} {y:+.2} {z:+.2} = {:.2} mg radius {:.1} residual {:.2} \
                 ({} held over {}/{} patches, isotropy {:.3}, {} offered {} gated)",
                x.hypot(y).hypot(z),
                g.radius,
                g.residual,
                g.samples,
                g.patches_seen,
                g.patches,
                g.isotropy,
                g.offered,
                g.gated
            );
        } else {
            debug!(
                "gravity: no fit ({} held over {}/{} patches, isotropy {:.3}, {} offered {} gated)",
                g.samples, g.patches_seen, g.patches, g.isotropy, g.offered, g.gated
            );
        }
        let e = self.session.evictions();
        debug!(
            "retention: {} evictions ({} by leverage, {} as outliers)",
            e.total, e.leverage, e.outlier
        );
    }

    /// Runs the timers that are due: the replay's next sample, and the
    /// quality update while collecting from a tag.
    pub fn fire_timers(&mut self, now: Instant) {
        if self.quit_due.is_some_and(|d| now >= d) {
            warn!("The tag did not detach in time; quitting anyway");
            self.quit_due = None;
            self.exit = true;
        }
        if self.replay_due.is_some_and(|d| now >= d) {
            self.replay_due = Some(now + self.config.replay_period);
            self.replay_tick();
        }
        if self.quality_due.is_some_and(|d| now >= d) {
            self.quality_due = Some(now + QUALITY_PERIOD);
            self.quality_update();
        }
    }

    /// One replay tick (`TriggerUpdate` in replay): the next sample, a quality
    /// update every second one, and at the end a last update and the stream
    /// off.
    fn replay_tick(&mut self) {
        let Some(replay) = self.replay.as_mut() else {
            return;
        };
        if replay.cursor < replay.samples.len() {
            let i = replay.cursor;
            let sample = replay.samples[i];
            replay.cursor += 1;
            let cursor = replay.cursor;
            self.sample(i as i32, 0, sample);
            if cursor % REPLAY_QUALITY_INTERVAL == 0 {
                self.quality_update();
            }
        } else {
            self.quality_update();
            self.set_stream(false);
            info!("replay complete");
        }
    }

    /// Applies one report from the tag thread.
    pub fn report(&mut self, report: Report) {
        match report {
            Report::BasesFound(bases) => match bases.len() {
                0 => {
                    self.attaching = false;
                    info!("No Tag Bases Found");
                    self.alert("Warning", "No Tag Bases Found", "warning");
                }
                1 => self.send(TagCmd::Attach(bases[0].0.clone())),
                _ => self.ask(PromptKind::ChooseBase(bases)),
            },
            Report::Attached(info, constants) => {
                self.attaching = false;
                self.attached = true;
                self.streaming = false;
                self.clear();
                self.info = Some(*info);
                if let Some(c) = constants {
                    self.load_constants(&c);
                }
                info!("Attach succeeded");
            }
            Report::AttachFailed(why) => {
                self.attaching = false;
                info!("{why}");
                self.alert("Warning", &why, "warning");
            }
            Report::Detached => {
                if self.quit_due.take().is_some() {
                    self.exit = true;
                }
                self.attached = false;
                self.streaming = false;
                self.collecting = false;
                self.info = None;
                self.live = None;
            }
            Report::Samples(samples) => {
                if self.streaming {
                    let batch = self.capture_batch;
                    self.capture_batch += 1;
                    for (i, s) in samples.into_iter().enumerate() {
                        self.sample(batch, i as i32, s);
                    }
                }
            }
            Report::Wrote(why) => {
                if why.is_empty() {
                    info!("Calibration written to the tag");
                } else {
                    info!("WriteCalibration failed: {why}");
                    self.alert("Tag Write", &why, "warning");
                }
            }
            Report::Read(Ok(c)) => self.load_constants(&c),
            Report::Read(Err(why)) => {
                info!("Read calibration failed: {why}");
                self.alert("Tag Read", &why, "warning");
            }
        }
    }

    // ---- Queries ------------------------------------------------------------

    /// Answers one UI query.
    pub fn query(&self, name: &str, _args: &Value) -> Value {
        match name {
            "menus" => super::view::menus(),
            "about" => json!({
                "title": "About Tag Calibrator",
                "text": format!(
                    "{}\n\nCollect magnetometer samples from a tag through its USB base, fit its \
                     magnetometer calibration, and save it to the tag.",
                    super::view::title()
                ),
            }),
            "user_guide_url" => json!(self.config.user_guide_url),
            other => query_error(&format!("unknown query {other}")),
        }
    }
}
