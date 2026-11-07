//! The thread that owns the tag: the Rust counterpart of qtmonitor's
//! `TagWorker` (`host/applications/qtmon/session/livetagsession.h`).
//!
//! It is the only code that touches the [`Tag`]. It polls the status every
//! [`STATUS_POLL_MS`] while
//! attached, runs each [`TagCmd`] in order, and runs downloads, which block
//! only it. Everything it learns goes back to the engine thread as a
//! [`Report`]; it decides nothing.

use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use log::{debug, warn};
use tagcore::proto::{Config, Status, TagInfo, TestReq};
use tagcore::tag::{STOP_RESEND_AFTER, is_acquiring, is_stopped};
use tagcore::{LinkAdapt, Tag, UsbDev};

use super::Msg;
use crate::model::commands::Command;
use crate::model::monitormodel::STATUS_POLL_MS;

/// Delay before the status read that follows Stop: the state machine acts on
/// a stop after acknowledging it.
const AFTER_STOP: Duration = Duration::from_millis(250);

/// How long after Stop the polls keep resending it while the tag is still
/// acquiring. An STM32L4 tag can accept a stop and lose it (see
/// [`STOP_RESEND_AFTER`]); past this window the model's own judgement of the
/// state stands.
const STOP_RESEND_FOR: Duration = Duration::from_secs(30);

/// How long the polls pause after Synchronize Clock or Self-Test is accepted.
///
/// While the tag sets its RTC or runs its self-tests, debug access to an L4
/// tag fails in bursts (base 0x81; TODO.md, Embedded > Bases), so polling then
/// only fills the log with errors. One status is read when the pause ends.
const QUIET_AFTER_BUSY: Duration = Duration::from_millis(1500);

/// A command for the tag thread.
#[derive(Debug)]
pub(crate) enum TagCmd {
    /// List the connected bases.
    FindBases,
    /// Attach to the tag on this base (resets it).
    Attach(UsbDev),
    /// Detach.
    Detach,
    /// Read the tag's configuration.
    ReadConfig,
    /// Write this configuration and start the tag.
    Start(Box<Config>),
    /// Stop the tag.
    Stop,
    /// Set the tag's clock from the host's.
    SyncClock,
    /// Run every self-test.
    SelfTest,
    /// Erase the tag's data.
    Erase,
    /// Download the log to this file.
    Download(PathBuf),
}

/// What the tag thread learned, for the engine thread.
#[derive(Debug)]
pub(crate) enum Report {
    /// The connected bases, possibly none, each with its identity when
    /// there are several to choose from (see [`identify`]).
    BasesFound(Vec<(UsbDev, Option<String>)>),
    /// Attached: the tag's identity and configuration.
    Attached(Box<TagInfo>, Box<Config>),
    /// Attaching failed; the text says so.
    AttachFailed(String),
    /// Detached.
    Detached,
    /// A status read, with the base's supply voltage and the host clock (ms).
    Status(Box<Status>, f32, i64),
    /// The tag's configuration, for Read Configuration.
    ConfigRead(Box<Config>),
    /// Start was sent; the text is Tag::start's error, or empty.
    StartSent(String),
    /// Self-Test was accepted.
    TestSent,
    /// Erase was accepted.
    EraseSent,
    /// The tag refused a command, or it could not be sent.
    CommandFailed(Command, String),
    /// Download progress: records written, and the record count.
    DownloadProgress(i32, i32),
    /// The download ended.
    DownloadFinished(super::download::DownloadResult),
}

/// The host clock, seconds since the epoch.
pub(crate) fn now_s() -> i64 {
    now_ms().div_euclid(1000)
}

/// The host clock, ms since the epoch.
pub(crate) fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// The base's identity (`base=...;fw=...;dirty=...;...`), from the tag
/// bases' identify command, or `None` for a base that predates it or cannot
/// be opened.
///
/// Opens the base's USB interface only, asks, and closes it: nothing is
/// sent to the target, so a tag on the base is not reset. Takes up to about
/// 300 ms for a base that does not answer. Not for a base the [`Tag`] has
/// open.
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
    /// Set by the engine to stop a running download after its page.
    cancel: Arc<AtomicBool>,
    /// When the next status read is due, while attached.
    next_poll: Option<Instant>,
    /// When an accepted Stop was first sent and when it was last sent, while
    /// the polls are still waiting for the tag to leave acquisition.
    stop_sent: Option<(Instant, Instant)>,
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

    /// Reads and reports the status, when attached; a failed read is skipped.
    ///
    /// When the read loses the probe (the base was unplugged), polling stops
    /// and [`Report::Detached`] is sent instead.
    fn poll(&mut self) {
        if !self.tag.is_attached() {
            self.next_poll = None;
            return;
        }
        let voltage = self.tag.voltage().unwrap_or(0.0);
        let status = self.tag.status();
        // A base unplugged during the poll closes the session in tagcore;
        // show the window detached rather than attached to nothing.
        if !self.tag.is_attached() {
            self.next_poll = None;
            self.report(Report::Detached);
            return;
        }
        if let Ok(status) = status {
            self.resend_stop(&status);
            self.report(Report::Status(Box::new(status), voltage, now_ms()));
        }
        self.next_poll = Some(Instant::now() + Duration::from_millis(STATUS_POLL_MS));
    }

    /// Sends the stop again when `status` shows the tag still acquiring
    /// [`STOP_RESEND_AFTER`] after it was last sent, for up to
    /// [`STOP_RESEND_FOR`]; forgets it once the tag has left acquisition.
    /// A resend that fails is only logged: the next poll decides.
    fn resend_stop(&mut self, status: &Status) {
        let Some((first, last)) = self.stop_sent else {
            return;
        };
        if !is_acquiring(status.state) || first.elapsed() >= STOP_RESEND_FOR {
            self.stop_sent = None;
            return;
        }
        if last.elapsed() >= STOP_RESEND_AFTER {
            warn!(
                "tag still acquiring {:.0} s after the stop was accepted; sending it again",
                last.elapsed().as_secs_f32()
            );
            if self.tag.stop().is_err() {
                warn!("resent stop failed: {}", self.tag.debug_message());
            }
            self.stop_sent = Some((first, Instant::now()));
        }
    }

    /// Runs one command.
    fn run(&mut self, cmd: TagCmd) {
        let attached = self.tag.is_attached();
        match cmd {
            TagCmd::FindBases => {
                let bases = self.tag.available();
                // One base needs no choosing, so it is not asked.
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
                    self.report(Report::AttachFailed("Already Attached to Tag".into()));
                } else if !self.tag.attach(&base) {
                    self.report(Report::AttachFailed("Could not attach to tag".into()));
                } else {
                    // A stop from an earlier session ended with it.
                    self.stop_sent = None;
                    let info = self.tag.tag_info().unwrap_or_default();
                    let config = self.tag.config().unwrap_or_default();
                    self.report(Report::Attached(Box::new(info), Box::new(config)));
                    self.poll();
                }
            }
            TagCmd::Detach => {
                if attached {
                    self.tag.detach();
                }
                self.next_poll = None;
                self.stop_sent = None;
                self.report(Report::Detached);
            }
            _ if !attached => {}
            TagCmd::ReadConfig => match self.tag.config() {
                Ok(config) => self.report(Report::ConfigRead(Box::new(config))),
                Err(_) => self.report(Report::CommandFailed(
                    Command::Read,
                    "Tag config read failed".into(),
                )),
            },
            TagCmd::Start(config) => {
                debug!(
                    "Starting tag with config: {}",
                    tagcore::proto::debug_string(&*config)
                );
                // A failed call does not mean the tag did not start: the
                // acknowledgement can be lost after the tag accepted it. The
                // model judges the start from the statuses that follow.
                let message = match self.tag.start(&config) {
                    Ok(_) => String::new(),
                    Err(_) => self.tag.debug_message(),
                };
                self.report(Report::StartSent(message));
                self.poll();
            }
            TagCmd::Stop => match self.tag.stop() {
                // Synchronous firmware has stopped already: show it at once,
                // with nothing to resend.
                Ok(Some(status)) if is_stopped(status.state) => {
                    self.stop_sent = None;
                    self.poll();
                }
                // Firmware that queues the stop: watch for it and resend.
                Ok(_) => {
                    let now = Instant::now();
                    self.stop_sent = Some((now, now));
                    self.next_poll = Some(now + AFTER_STOP);
                }
                Err(_) => self.report(Report::CommandFailed(
                    Command::Stop,
                    "The tag did not accept the stop command.".into(),
                )),
            },
            TagCmd::SyncClock => match self.tag.set_rtc() {
                Ok(()) => self.next_poll = Some(Instant::now() + QUIET_AFTER_BUSY),
                Err(_) => {
                    let message = self.tag.debug_message();
                    self.report(Report::CommandFailed(Command::Sync, message));
                }
            },
            TagCmd::SelfTest => match self.tag.test(TestReq::RunAll) {
                Ok(_) => {
                    self.report(Report::TestSent);
                    self.next_poll = Some(Instant::now() + QUIET_AFTER_BUSY);
                }
                Err(_) => {
                    let message = self.refusal("The tag did not accept the self-test command.");
                    self.report(Report::CommandFailed(Command::Test, message));
                }
            },
            TagCmd::Erase => match self.tag.erase() {
                Ok(_) => {
                    self.report(Report::EraseSent);
                    self.poll();
                }
                Err(_) => self.report(Report::CommandFailed(
                    Command::Erase,
                    "The tag did not accept the erase command.".into(),
                )),
            },
            TagCmd::Download(path) => {
                self.cancel.store(false, Ordering::Relaxed);
                let out = self.out.clone();
                let result = super::download::run(&self.tag, &path, &self.cancel, |v, m| {
                    let _ = out.send(Msg::Tag(Report::DownloadProgress(v, m)));
                });
                self.report(Report::DownloadFinished(result));
                self.poll();
            }
        }
    }
}

/// The tag thread's loop: commands in order, and a status read whenever one
/// is due. Ends, detaching first, when the engine drops its sender.
pub(crate) fn run(rx: Receiver<TagCmd>, out: Sender<Msg>, cancel: Arc<AtomicBool>) {
    let mut t = TagThread {
        tag: Tag::new(),
        out,
        cancel,
        next_poll: None,
        stop_sent: None,
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
