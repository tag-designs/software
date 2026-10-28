//! The thread that owns the tag.
//!
//! It is the only code that touches the [`Tag`]. It runs each [`TagCmd`] in
//! order and, while streaming, reads the tag's calibration log every
//! [`SAMPLE_POLL`], as qtcalibrate's sample timer does, reporting each batch
//! of samples. While attached but not streaming it reads the status every
//! [`KEEP_AWAKE`], which keeps the tag from sleeping. Everything it learns
//! goes back to the engine thread as a [`Report`]; it decides nothing.

use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant};

use log::{info, warn};
use tagcore::proto::{CalibrationConstants, TagInfo, TagState, ack};
use tagcore::{LinkAdapt, Tag, UsbDev};

use super::Msg;

/// How often the calibration log is read while streaming: qtcalibrate's
/// 100 ms sample timer.
pub(crate) const SAMPLE_POLL: Duration = Duration::from_millis(100);

/// How long to wait for a stopped calibration to reach IDLE. The tag
/// acknowledges Stop before its main thread acts on it, so the state is
/// polled rather than assumed (docs/bench/verifying-firmware.md: an
/// acknowledgement is not a completion).
const STOP_SETTLE: Duration = Duration::from_millis(2000);

/// How often the status is read while attached and not streaming. A tag
/// sleeps after about 4.5 s without a host request; Tag Monitor polls every
/// 400 ms for the same reason.
const KEEP_AWAKE: Duration = Duration::from_millis(1000);

/// One calibration sample: the magnetometer reading, and the accelerometer's
/// when the tag sent one.
pub(crate) type Sample = ([f32; 3], Option<[f32; 3]>);

/// A command for the tag thread.
#[derive(Debug)]
pub(crate) enum TagCmd {
    /// List the connected bases.
    FindBases,
    /// Attach to the tag on this base (resets it).
    Attach(UsbDev),
    /// Detach.
    Detach,
    /// Start (set the clock, enter calibration) or stop streaming samples.
    Stream(bool),
    /// Append these constants to the tag's calibration store.
    WriteCalibration(Box<CalibrationConstants>),
    /// Read the tag's last stored constants.
    ReadCalibration,
}

/// What the tag thread learned, for the engine thread.
#[derive(Debug)]
pub(crate) enum Report {
    /// The connected bases, each with its identity when there are several.
    BasesFound(Vec<(UsbDev, Option<String>)>),
    /// Attached: the tag's identity, and its stored constants if it has any.
    Attached(Box<TagInfo>, Option<Box<CalibrationConstants>>),
    /// Attaching failed, or the tag was not IDLE; the text says which.
    AttachFailed(String),
    /// Detached.
    Detached,
    /// One read of the calibration log: its samples, in order.
    Samples(Vec<Sample>),
    /// Tag Write finished: empty on success, else why it failed.
    Wrote(String),
    /// Tag Read finished: the constants, or why there were none.
    Read(Result<Box<CalibrationConstants>, String>),
}

/// The base's identity string, from the tag bases' identify command, or
/// `None` for a base that predates it or cannot be opened. Opens the base's
/// USB interface only, so a tag on it is not reset (as in Tag Monitor).
fn identify(base: &UsbDev) -> Option<String> {
    let mut link = LinkAdapt::new();
    if !link.open(base) {
        return None;
    }
    let identity = link.base_identity();
    link.detach();
    identity
}

/// The tag thread's state.
struct TagThread {
    /// The tag; touched only on this thread.
    tag: Tag,
    /// Where reports go: the engine thread's channel.
    out: Sender<Msg>,
    /// Whether the tag is streaming calibration samples.
    streaming: bool,
    /// When the next read (samples or keep-awake status) is due.
    next_poll: Option<Instant>,
}

impl TagThread {
    /// Sends a report to the engine thread; a gone engine is ignored.
    fn report(&self, report: Report) {
        let _ = self.out.send(Msg::Tag(report));
    }

    /// The tag's message for the last refused call, or `fallback`.
    fn refusal(&self, fallback: &str) -> String {
        let message = self.tag.debug_message();
        if message.is_empty() {
            fallback.to_string()
        } else {
            message
        }
    }

    /// Whether the tag reports the CALIBRATE state.
    fn calibrating(&self) -> bool {
        self.tag
            .status()
            .is_ok_and(|s| s.state() == TagState::Calibrate)
    }

    /// Stops a calibration and waits, up to [`STOP_SETTLE`], for the tag to
    /// report IDLE. The CALIBRATE state is kept in the tag's backup registers,
    /// so a tag detached before it acts on Stop stays calibrating -- through
    /// the reset of the next attach -- and refuses everything but another
    /// Stop.
    ///
    /// Returns whether the tag reached IDLE.
    fn stop_calibration(&mut self) -> bool {
        let _ = self.tag.stop();
        let deadline = Instant::now() + STOP_SETTLE;
        loop {
            match self.tag.status().map(|s| s.state()) {
                Ok(TagState::Idle) => return true,
                _ if Instant::now() >= deadline => {
                    warn!("The tag did not return to IDLE after Stop");
                    return false;
                }
                _ => std::thread::sleep(Duration::from_millis(100)),
            }
        }
    }

    /// Reads the calibration log while streaming, or the status otherwise,
    /// and schedules the next read.
    fn poll(&mut self) {
        if !self.tag.is_attached() {
            self.next_poll = None;
            return;
        }
        if self.streaming {
            if let Ok(ackn) = self.tag.calibration_log()
                && let Some(ack::Payload::CalibrationLog(log)) = ackn.payload
            {
                let samples = log
                    .data
                    .iter()
                    .filter_map(|d| {
                        let m = d.mag.as_ref()?;
                        let a = d.accel.as_ref().map(|a| [a.ax, a.ay, a.az]);
                        Some(([m.mx, m.my, m.mz], a))
                    })
                    .collect();
                self.report(Report::Samples(samples));
            }
            self.next_poll = Some(Instant::now() + SAMPLE_POLL);
        } else {
            let _ = self.tag.status();
            self.next_poll = Some(Instant::now() + KEEP_AWAKE);
        }
    }

    /// Runs one command.
    fn run(&mut self, cmd: TagCmd) {
        let attached = self.tag.is_attached();
        match cmd {
            TagCmd::FindBases => {
                let bases = self.tag.available();
                let several = bases.len() > 1 && !attached;
                let found = bases
                    .into_iter()
                    .map(|b| {
                        let id = if several { identify(&b) } else { None };
                        (b, id)
                    })
                    .collect();
                self.report(Report::BasesFound(found));
            }
            TagCmd::Attach(base) => {
                if attached {
                    self.report(Report::AttachFailed("Already attached to tag".into()));
                } else if !self.tag.attach(&base) {
                    self.report(Report::AttachFailed("Could not attach to tag".into()));
                } else {
                    let info = self.tag.tag_info().unwrap_or_default();
                    // A tag left streaming -- by a program that exited
                    // without stopping it -- comes back still calibrating;
                    // stop it rather than refuse it.
                    if self.calibrating() {
                        info!("The tag was still calibrating; stopping it");
                        self.stop_calibration();
                    }
                    let idle = self.tag.status().is_ok_and(|s| s.state() == TagState::Idle);
                    if !idle {
                        // As qtcalibrate: a tag that is running or holds data
                        // is not one to calibrate.
                        self.tag.detach();
                        self.report(Report::AttachFailed("Tag not in idle state".into()));
                    } else {
                        let constants = self
                            .tag
                            .read_calibration(-1)
                            .ok()
                            .filter(|c| c.magnetometer.is_some())
                            .map(Box::new);
                        self.streaming = false;
                        self.report(Report::Attached(Box::new(info), constants));
                        self.next_poll = Some(Instant::now() + KEEP_AWAKE);
                    }
                }
            }
            TagCmd::Detach => {
                if attached {
                    if self.streaming {
                        self.stop_calibration();
                    }
                    self.tag.detach();
                }
                self.streaming = false;
                self.next_poll = None;
                self.report(Report::Detached);
            }
            _ if !attached => {}
            TagCmd::Stream(on) => {
                if on {
                    // qtcalibrate sets the clock and enters calibration.
                    let _ = self.tag.set_rtc();
                    let _ = self.tag.calibrate();
                } else {
                    self.stop_calibration();
                }
                self.streaming = on;
                self.next_poll = Some(Instant::now() + if on { SAMPLE_POLL } else { KEEP_AWAKE });
            }
            TagCmd::WriteCalibration(constants) => match self.tag.write_calibration(&constants) {
                Ok(()) => self.report(Report::Wrote(String::new())),
                Err(_) => {
                    let why = self.refusal("The tag did not accept the calibration.");
                    self.report(Report::Wrote(why));
                }
            },
            TagCmd::ReadCalibration => match self.tag.read_calibration(-1) {
                Ok(c) if c.magnetometer.is_some() => self.report(Report::Read(Ok(Box::new(c)))),
                Ok(_) => self.report(Report::Read(Err("The tag holds no calibration.".into()))),
                Err(_) => self.report(Report::Read(Err(self.refusal("Read calibration failed")))),
            },
        }
    }
}

/// The tag thread's loop: commands in order, and a read whenever one is due.
/// Ends, detaching first, when the engine drops its sender.
pub(crate) fn run(rx: Receiver<TagCmd>, out: Sender<Msg>) {
    let mut t = TagThread {
        tag: Tag::new(),
        out,
        streaming: false,
        next_poll: None,
    };
    loop {
        let wait = t
            .next_poll
            .map(|due| due.saturating_duration_since(Instant::now()))
            .unwrap_or(Duration::from_secs(3600));
        match rx.recv_timeout(wait) {
            Ok(cmd) => t.run(cmd),
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => {
                if t.tag.is_attached() {
                    if t.streaming {
                        t.stop_calibration();
                    }
                    t.tag.detach();
                }
                return;
            }
        }
        if t.next_poll.is_some_and(|due| Instant::now() >= due) {
            t.poll();
        }
    }
}
