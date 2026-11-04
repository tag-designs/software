//! The tag interface the host tools use: one method per monitor request.
//!
//! Port of `tagclass.*`. [`Tag`] owns a [`TagMonitor`] behind a mutex, so one
//! `Tag` can be shared between threads (for example in an `Arc`); every call
//! holds the lock for its whole exchange, so requests never interleave on
//! the link.
//!
//! Requests that go to the firmware return `Result<_, TagError>`. The
//! outcome of the most recent one is also kept, as in the C++ class, and is
//! available from [`Tag::last_error`] and [`Tag::debug_message`].
//!
//! Link-level calls that send no request to the firmware (voltage, git SHA,
//! base identity, live memory access, keep-alive and the field-rescue
//! helpers) return `Option`, `bool` or [`MemAccess`] with the reason in the
//! log, as the C++ only logs it, and do not change the recorded outcome.
//!
//! # Errors
//!
//! Every method returning `Result<_, TagError>` fails in one of three ways:
//!
//! - the RPC did not complete over the link (not attached, USB or SWD
//!   failure, monitor timeout): [`TagError::transport_failed`] is set and
//!   the message is "Monitor call failed" unless noted otherwise;
//! - the firmware refused: `err` is its [`ack::Err`] code and the message
//!   is the firmware's text, or `"Tag returned <CODE>"`;
//! - the reply was OK but lacked the expected payload: the message is
//!   `"Tag reply carried no <what>"`.
//!
//! Methods that return a whole [`Ack`] ([`Tag::request`],
//! [`Tag::data_log`], [`Tag::calibration_log`]) fail only in the first way.
//!
//! # Blocking
//!
//! Every method blocks while it holds the lock, so one slow request (an
//! STM32L4 protobuf call can take up to about 120 s before timing out)
//! stalls every other thread using the same `Tag`. [`Tag::set_rtc`] also
//! sleeps up to one second, with the lock held, to align to a second
//! boundary.
//!
//! # Attaching resets the tag
//!
//! [`Tag::attach`] goes through reset, so it ends a running tag's run. See
//! the crate docs and `docs/bench/verifying-firmware.md`.

use std::fmt;
use std::sync::{Mutex, MutexGuard};
use std::thread::sleep;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use log::warn;

use crate::link::{LinkAdaptStats, UsbDev};
use crate::monitor::{BitTagFlashLog, MemAccess, TagMonitor, TagMonitorStats, TargetFamily};
use crate::proto::{
    Ack, CalibrationConstants, Config, Empty, LogReq, Req, StateLog, Status, TagInfo, TagState,
    TestReq, ack, log_req, req,
};

/// Why a monitor RPC made through a [`Tag`] failed.
///
/// Separates a link or monitor failure, where the request may never have
/// reached the firmware, from a refusal by the firmware, which arrives as an
/// [`ack::Err`] code with an optional message.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TagError {
    /// The RPC did not complete over the link.
    pub transport_failed: bool,
    /// The firmware's Ack code; `Unspecified` after a transport failure.
    pub err: ack::Err,
    /// Human-readable reason; empty only in [`Tag::last_error`] after a
    /// successful call.
    pub message: String,
}

impl fmt::Display for TagError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for TagError {}

/// Interval between the status reads [`Tag::stop_confirmed`] makes while it
/// waits for the tag to stop.
pub const STOP_POLL: Duration = Duration::from_secs(2);

/// How long a stop may go unacted on, with the tag still acquiring, before
/// [`Tag::stop_confirmed`] sends it again. Applies only to firmware that
/// answers a stop with a bare OK.
///
/// That firmware queued the stop for a later state-machine pass, and an
/// STM32L4 tag could accept one and lose it: a stop sent in the first second
/// or so of the first session after a start was answered OK and never acted
/// on, while the same request sent later in the same session stopped the tag
/// at once. A stop is harmless to repeat while the tag is acquiring, so the
/// cure on the host is to resend it rather than wait out the timeout.
/// Firmware with synchronous commands stops before it answers, and replies
/// with the resulting Status, so nothing is resent to it.
pub const STOP_RESEND_AFTER: Duration = Duration::from_secs(4);

/// Whether a stop ends an acquisition in `state`: CONFIGURED, RUNNING or
/// HIBERNATING, the states the firmware's `monitor_acquisition_active()`
/// accepts. A host sends a stop in no other state.
pub fn is_acquiring(state: i32) -> bool {
    state == TagState::Configured as i32
        || state == TagState::Running as i32
        || state == TagState::Hibernating as i32
}

/// Whether `state` is one a stop ends in: FINISHED or ABORTED.
pub fn is_stopped(state: i32) -> bool {
    state == TagState::Finished as i32 || state == TagState::Aborted as i32
}

/// Why [`Tag::stop_confirmed`] did not leave the tag stopped.
#[derive(Debug, Clone, PartialEq)]
pub enum StopFailure {
    /// A status read failed, so the tag's state is unknown.
    Status(TagError),
    /// The tag is not acquiring, so no stop was sent; carries its status.
    NotAcquiring(Box<Status>),
    /// The tag refused the first stop.
    Refused(TagError),
    /// The tag had not stopped when the timeout ran out; carries the last
    /// status read.
    TimedOut(Box<Status>),
}

impl fmt::Display for StopFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Status(e) => write!(f, "Could not read tag status: {e}"),
            Self::NotAcquiring(s) => write!(
                f,
                "Tag is {}, not acquiring; no stop sent",
                state_label(s.state)
            ),
            Self::Refused(e) => write!(f, "Stop failed: {e}"),
            Self::TimedOut(s) => write!(
                f,
                "Tag did not reach a stopped state in time; last state {}",
                state_label(s.state)
            ),
        }
    }
}

impl std::error::Error for StopFailure {}

/// The protobuf name of `state`, or its number when it is out of range.
fn state_label(state: i32) -> String {
    TagState::try_from(state)
        .map(|s| s.as_str_name().to_owned())
        .unwrap_or_else(|_| state.to_string())
}

/// The firmware's Ack code, treating an out-of-range value as `Unspecified`.
fn ack_err(ack: &Ack) -> ack::Err {
    ack::Err::try_from(ack.err).unwrap_or(ack::Err::Unspecified)
}

/// The outcome of an RPC, as `Tag::LastError()` reports it: the firmware's
/// message if it sent one, otherwise one derived from the transport flag and
/// the Ack code, naming the tag's state when the refusal carries a Status;
/// empty for a success.
fn describe(ack: &Ack, rpc_failed: bool) -> TagError {
    let err = ack_err(ack);
    let mut message = ack.error_message().to_owned();
    if message.is_empty() {
        if rpc_failed {
            message = "Monitor call failed".into();
        } else if err != ack::Err::Ok {
            message = format!("Tag returned {}", err.as_str_name());
            if let Some(ack::Payload::Status(status)) = &ack.payload {
                message.push_str(&format!(" (tag is {})", state_label(status.state)));
            }
        }
    }
    TagError {
        transport_failed: rpc_failed,
        err,
        message,
    }
}

/// An Ack that records a host-side failure, as the C++ `Start()` and
/// `SetRtc()` leave behind.
fn monitor_failure(message: &str) -> Ack {
    Ack {
        err: ack::Err::Monitor as i32,
        payload: Some(ack::Payload::ErrorMessage(message.into())),
    }
}

/// The outcome of a state-changing command (Start, Stop, Erase, Self-test,
/// Calibrate) whose RPC completed with `ack`.
///
/// Firmware with synchronous commands carries out the transition before it
/// answers, and replies with the resulting [`Status`]; older firmware answers
/// a bare OK and acts later. See
/// `embedded/tags/common/core/design/monitor-commands.md`.
///
/// # Errors
///
/// The [`describe`]d Ack when its code is not OK: PERM when the state did
/// not allow the command, NXIO when a start or calibrate did not reach its
/// state, and so on.
fn command_outcome(ack: &Ack) -> Result<Option<Status>, TagError> {
    if ack_err(ack) != ack::Err::Ok {
        return Err(describe(ack, false));
    }
    match &ack.payload {
        Some(ack::Payload::Status(status)) => Ok(Some(status.clone())),
        _ => Ok(None),
    }
}

/// Wraps `payload` in a [`Req`].
fn request(payload: req::Payload) -> Req {
    Req {
        payload: Some(payload),
    }
}

/// A log request for block `index` of the log `fmt` selects.
///
/// The protobuf field is unsigned; a negative `index` is sent as its
/// two's-complement bit pattern, as the C++ implicit conversion does.
fn log_request(fmt: log_req::LogDataType, index: i32) -> Req {
    request(req::Payload::Log(LogReq {
        fmt: fmt as i32,
        index: index as u32,
    }))
}

/// State behind the [`Tag`] lock.
struct Inner {
    /// The monitor session; all link traffic goes through it.
    monitor: TagMonitor,
    /// The most recent reply, or the failure recorded in its place.
    ack: Ack,
    /// The last RPC did not complete over the link.
    rpc_failed: bool,
}

impl Inner {
    /// Sends `req` and records the reply (or its absence) for `last_error`.
    fn rpc(&mut self, req: &Req) -> bool {
        match self.monitor.rpc(req) {
            Some(ack) => {
                self.ack = ack;
                self.rpc_failed = false;
            }
            None => {
                self.ack = Ack::default();
                self.rpc_failed = true;
            }
        }
        !self.rpc_failed
    }

    /// The recorded outcome of the last RPC as a [`TagError`]; its message
    /// is empty if that RPC succeeded.
    fn error(&self) -> TagError {
        describe(&self.ack, self.rpc_failed)
    }

    /// The error for a reply that completed but lacked the expected payload.
    fn missing(&self, what: &str) -> TagError {
        let mut error = self.error();
        if error.message.is_empty() {
            error.message = format!("Tag reply carried no {what}");
        }
        error
    }

    /// Ok if the RPC completed and the firmware answered OK.
    fn expect_ok(&mut self, req: &Req) -> Result<(), TagError> {
        if self.rpc(req) && ack_err(&self.ack) == ack::Err::Ok {
            Ok(())
        } else {
            Err(self.error())
        }
    }

    /// Sends a state-changing command; see [`command_outcome`].
    ///
    /// A transport failure is recorded as a MONITOR Ack ("Monitor call
    /// failed"), as the C++ `Start()` does, so [`Tag::last_error`] reports
    /// [`ack::Err::Monitor`] rather than `Unspecified`.
    fn command(&mut self, req: &Req) -> Result<Option<Status>, TagError> {
        if !self.rpc(req) {
            self.ack = monitor_failure("Monitor call failed");
            return Err(self.error());
        }
        command_outcome(&self.ack)
    }

    /// The Ack payload, if the RPC completed and carried one.
    fn payload(&mut self, req: &Req) -> Option<ack::Payload> {
        if self.rpc(req) {
            self.ack.payload.clone()
        } else {
            None
        }
    }
}

/// A tag reached through an ST-LINK, and its firmware's monitor.
///
/// Create one, [`attach`](Self::attach) (which resets the tag), make
/// requests, and [`detach`](Self::detach); dropping it detaches too.
///
/// Port of the C++ `Tag` (`tagclass.*`). Methods take `&self` and
/// serialise on an internal mutex, so one `Tag` can be shared
/// in an `Arc`. Requests made while detached fail as transport errors.
///
/// # Examples
///
/// ```no_run
/// use tagcore::{Tag, UsbDev};
///
/// let tag = Tag::new();
/// if tag.attach(&UsbDev::default()) {
///     // The tag has just been reset.
///     let status = tag.status()?;
///     println!("{:?}", status.state());
///     tag.detach();
/// }
/// # Ok::<(), tagcore::TagError>(())
/// ```
pub struct Tag {
    /// The monitor and the last RPC outcome, behind one lock so a request
    /// and the recording of its outcome are atomic.
    inner: Mutex<Inner>,
}

/// Same as [`Tag::new`].
impl Default for Tag {
    fn default() -> Self {
        Self::new()
    }
}

impl Tag {
    /// Creates a detached tag with its own libusb session.
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(Inner {
                monitor: TagMonitor::new(),
                ack: Ack::default(),
                rpc_failed: false,
            }),
        }
    }

    /// Locks the inner state, blocking until any other thread's request
    /// finishes.
    fn lock(&self) -> MutexGuard<'_, Inner> {
        // A panic in another thread leaves nothing half-written that a later
        // call cannot cope with, so a poisoned lock is still used.
        self.inner.lock().unwrap_or_else(|e| e.into_inner())
    }

    // ---- Monitor interface ----

    /// Lists the accepted ST-LINKs on USB. Opens none for debug, so no tag is
    /// reset.
    pub fn available(&self) -> Vec<UsbDev> {
        self.lock().monitor.available()
    }

    /// Attaches to the tag on the ST-LINK `usbdev` names, or on the first
    /// ST-LINK found when its `vid` or `pid` is zero (`UsbDev::default()`).
    /// **Resets the tag.** Returns false, with the reason logged, if no probe
    /// matched or the monitor did not start.
    ///
    /// Also false, without touching the probe, if already attached. See
    /// [`TagMonitor::attach`] for the sequence.
    pub fn attach(&self, usbdev: &UsbDev) -> bool {
        let mut inner = self.lock();
        if usbdev.vid != 0 && usbdev.pid != 0 {
            return inner.monitor.attach(usbdev);
        }
        match inner.monitor.available().first() {
            Some(first) => inner.monitor.attach(first),
            None => false,
        }
    }

    /// Ends the monitor session and releases the probe.
    ///
    /// Sends MONITORSTOP, after which the tag may enter low power at once.
    /// Harmless when not attached.
    pub fn detach(&self) {
        self.lock().monitor.detach();
    }

    /// True while a monitor session is open.
    pub fn is_attached(&self) -> bool {
        self.lock().monitor.is_attached()
    }

    /// The MCU family detected at attach; `Unknown` when detached.
    pub fn target_family(&self) -> TargetFamily {
        self.lock().monitor.target_family()
    }

    /// Zeroes the link statistics.
    pub fn reset_link_stats(&self) {
        self.lock().monitor.reset_link_stats();
    }

    /// The USB and ST-LINK statistics.
    pub fn link_stats(&self) -> LinkAdaptStats {
        self.lock().monitor.link_stats()
    }

    /// Zeroes the RPC statistics.
    pub fn reset_monitor_stats(&self) {
        self.lock().monitor.reset_monitor_stats();
    }

    /// The RPC statistics.
    pub fn monitor_stats(&self) -> TagMonitorStats {
        self.lock().monitor.monitor_stats()
    }

    /// The tag's supply voltage as the ST-LINK measures it, in volts.
    pub fn voltage(&self) -> Option<f32> {
        self.lock().monitor.voltage()
    }

    /// The base's identity from the tag bases' identify command; see
    /// [`LinkAdapt::base_identity`](crate::LinkAdapt::base_identity). `None`
    /// when the base predates the command or no base is attached.
    pub fn base_identity(&self) -> Option<String> {
        self.lock().monitor.link.base_identity()
    }

    /// The firmware's git SHA, read at attach; `None` when detached.
    pub fn git_sha(&self) -> Option<String> {
        let inner = self.lock();
        inner
            .monitor
            .is_attached()
            .then(|| inner.monitor.git_sha_string())
    }

    // ---- Tag information ----

    /// Static tag information: type, UUID, firmware and so on.
    pub fn tag_info(&self) -> Result<TagInfo, TagError> {
        let mut inner = self.lock();
        match inner.payload(&request(req::Payload::GetInfo(Empty {}))) {
            Some(ack::Payload::Info(info)) => Ok(info),
            _ => Err(inner.missing("tag information")),
        }
    }

    /// The configuration the tag is running, or will run.
    pub fn config(&self) -> Result<Config, TagError> {
        let mut inner = self.lock();
        match inner.payload(&request(req::Payload::GetConfig(Empty {}))) {
            Some(ack::Payload::Config(config)) => Ok(config),
            _ => Err(inner.missing("configuration")),
        }
    }

    /// Current state, clock, data counts, voltage and temperature.
    pub fn status(&self) -> Result<Status, TagError> {
        let mut inner = self.lock();
        match inner.payload(&request(req::Payload::GetStatus(Empty {}))) {
            Some(ack::Payload::Status(status)) => Ok(status),
            _ => Err(inner.missing("status")),
        }
    }

    // ---- Field rescue, used by tag-dwnld --rescue-exception ----

    /// Overwrites the persisted state word of an old BitTag; see
    /// [`TagMonitor::force_backup_state`].
    pub fn force_backup_state(&self, state: TagState) -> bool {
        self.lock().monitor.force_backup_state(state)
    }

    /// Counts an old BitTag's records in flash; see
    /// [`TagMonitor::count_bittag_log_headers`].
    pub fn count_bittag_log_headers(&self, layout: BitTagFlashLog) -> Option<usize> {
        self.lock().monitor.count_bittag_log_headers(layout)
    }

    /// Reads an old BitTag's records straight from flash as a data-log Ack;
    /// see [`TagMonitor::read_bittag_log_from_flash`].
    pub fn bittag_log_from_flash(&self, layout: BitTagFlashLog, index: i32) -> Option<Ack> {
        self.lock().monitor.read_bittag_log_from_flash(layout, index)
    }

    // ---- Live memory access ----

    /// Reads live tag memory or peripheral space between RPCs; see
    /// [`TagMonitor::read_mem`].
    pub fn read_memory(&self, addr: u32, buf: &mut [u8]) -> MemAccess {
        self.lock().monitor.read_mem(addr, buf)
    }

    /// Writes live tag memory or peripheral space between RPCs; see
    /// [`TagMonitor::write_mem`]. Nothing is checked beyond the U3
    /// monitor_shared guard that `force` overrides.
    pub fn write_memory(&self, addr: u32, data: &[u8], force: bool) -> MemAccess {
        self.lock().monitor.write_mem(addr, data, force)
    }

    /// Keeps the session alive while the caller is idle; see
    /// [`TagMonitor::keep_alive`]. Call at least every 2-3 s for an STM32U3
    /// tag when nothing else is talking to it.
    pub fn keep_alive(&self) -> bool {
        self.lock().monitor.keep_alive()
    }

    // ---- Raw requests and outcomes ----

    /// Sends any request and returns the reply as is, whatever its Ack code.
    ///
    /// For callers that forward the monitor protocol, such as a server's raw
    /// call, so that a request without a dedicated method is still usable.
    /// `Err` only when the RPC did not complete over the link.
    pub fn request(&self, req: &Req) -> Result<Ack, TagError> {
        let mut inner = self.lock();
        if inner.rpc(req) {
            Ok(inner.ack.clone())
        } else {
            Err(inner.error())
        }
    }

    /// The outcome of the most recent request sent to the firmware.
    ///
    /// Covers the calls that send a request (status, start, logs and so on);
    /// memory access, keep-alive and the field-rescue helpers do not change
    /// it. The message is empty if that request succeeded.
    pub fn last_error(&self) -> TagError {
        self.lock().error()
    }

    /// The firmware's message from the last reply, or a description of its
    /// Ack code; empty if the last reply was OK.
    pub fn debug_message(&self) -> String {
        let inner = self.lock();
        let message = inner.ack.error_message();
        if !message.is_empty() {
            return message.to_owned();
        }
        match ack_err(&inner.ack) {
            ack::Err::Ok => String::new(),
            ack::Err::Unspecified => "Tag returned an empty or invalid acknowledgement".into(),
            _ => describe(&inner.ack, false).message,
        }
    }

    // ---- Tag control ----
    //
    // Start, Stop, Erase, Self-test and Calibrate return `Ok(Some(status))`
    // from firmware with synchronous commands: the transition has run, and
    // `status` is the state it reached. `Ok(None)` is a bare OK from older
    // firmware, which only queued the command; the caller then learns the
    // outcome from later status reads. A refusal is `Err`; from the newer
    // firmware a PERM leaves the state as it was.

    /// Erases the stored data and returns the tag to IDLE.
    ///
    /// Synchronous firmware replies on entering sRESET, before the sweep: the
    /// erase then runs on the tag, and the caller polls until IDLE. Accepted
    /// from FINISHED and ABORTED, and from sRESET, where it continues the
    /// sweep already under way and replies sRESET. Older firmware refuses an
    /// erase in sRESET with PERM, but its sweep carries on regardless.
    ///
    /// # Errors
    ///
    /// The transport failure or the firmware's refusal; see [`Tag::last_error`].
    pub fn erase(&self) -> Result<Option<Status>, TagError> {
        self.lock()
            .command(&request(req::Payload::Erase(Empty {})))
    }

    /// Configures the tag with `config` and starts it.
    ///
    /// Synchronous firmware replies CONFIGURED or RUNNING. It answers NXIO
    /// when the configuration could not be stored, or when the start ended in
    /// any other state (a start that fails device setup aborts).
    ///
    /// # Errors
    ///
    /// The transport failure, recorded as a MONITOR Ack ("Monitor call
    /// failed"), or the firmware's refusal; see [`Tag::last_error`].
    pub fn start(&self, config: &Config) -> Result<Option<Status>, TagError> {
        self.lock()
            .command(&request(req::Payload::Start(config.clone())))
    }

    /// Stops a running tag, or ends calibration.
    ///
    /// Synchronous firmware replies FINISHED or ABORTED (IDLE when it ends
    /// calibration). Older firmware only queues the stop, and an STM32L4 tag
    /// running it can accept the stop and lose it (see
    /// [`STOP_RESEND_AFTER`]); use [`Tag::stop_confirmed`] unless something
    /// else handles both.
    ///
    /// # Errors
    ///
    /// The transport failure or the firmware's refusal; see [`Tag::last_error`].
    pub fn stop(&self) -> Result<Option<Status>, TagError> {
        self.lock()
            .command(&request(req::Payload::Stop(Empty {})))
    }

    /// Stops an acquiring tag and waits until it reports FINISHED or ABORTED.
    ///
    /// Reads the status first. A tag already FINISHED or ABORTED is returned
    /// as it is, with no stop sent; one in any other state that is not
    /// acquiring (see [`is_acquiring`]) is refused without sending one. The
    /// stop is then sent. A reply carrying a stopped Status (synchronous
    /// firmware) is returned at once. Otherwise the status is read every
    /// [`STOP_POLL`]; while the
    /// tag is still acquiring [`STOP_RESEND_AFTER`] after the last stop, the
    /// stop is sent again. A resent stop that fails is only logged: the state
    /// may have moved between the read and the request, and the next read
    /// decides.
    ///
    /// Blocks for up to `timeout` plus one poll, taking and releasing the
    /// lock for each request.
    ///
    /// # Errors
    ///
    /// [`StopFailure::Status`] when any status read fails;
    /// [`StopFailure::NotAcquiring`] when the tag is neither acquiring nor
    /// stopped; [`StopFailure::Refused`] when the first stop is refused;
    /// [`StopFailure::TimedOut`] when the tag has not stopped by `timeout`.
    pub fn stop_confirmed(&self, timeout: Duration) -> Result<Status, StopFailure> {
        let status = self.status().map_err(StopFailure::Status)?;
        if is_stopped(status.state) {
            return Ok(status);
        }
        if !is_acquiring(status.state) {
            return Err(StopFailure::NotAcquiring(Box::new(status)));
        }
        if let Some(stopped) = self.stop().map_err(StopFailure::Refused)?
            && is_stopped(stopped.state)
        {
            return Ok(stopped);
        }
        let started = Instant::now();
        let mut sent = started;
        loop {
            let status = self.status().map_err(StopFailure::Status)?;
            if is_stopped(status.state) {
                return Ok(status);
            }
            if started.elapsed() >= timeout {
                return Err(StopFailure::TimedOut(Box::new(status)));
            }
            if is_acquiring(status.state) && sent.elapsed() >= STOP_RESEND_AFTER {
                warn!(
                    "tag still {} {:.0} s after the stop was accepted; sending it again",
                    state_label(status.state),
                    sent.elapsed().as_secs_f32()
                );
                if let Err(e) = self.stop() {
                    warn!("resent stop failed: {e}");
                }
                sent = Instant::now();
            }
            sleep(STOP_POLL);
        }
    }

    /// Puts the tag in its sensor-calibration state; [`Tag::stop`] ends it.
    ///
    /// Synchronous firmware replies CALIBRATE, or NXIO if the tag did not
    /// reach it. Calibration does not survive an attach: the reset returns
    /// the tag to IDLE.
    ///
    /// # Errors
    ///
    /// The transport failure or the firmware's refusal; see [`Tag::last_error`].
    pub fn calibrate(&self) -> Result<Option<Status>, TagError> {
        self.lock()
            .command(&request(req::Payload::Calibrate(Empty {})))
    }

    /// Sets the tag's clock to the host's.
    ///
    /// Waits for the next whole second of system time, then sends the time
    /// in milliseconds since the Unix epoch, so the tag's clock lands close
    /// to a second boundary. Blocks for up to one second, holding the lock.
    ///
    /// # Errors
    ///
    /// A transport failure is recorded as a MONITOR Ack ("Monitor call
    /// failed while setting RTC"); an UNSPECIFIED (empty or undecodable) Ack
    /// is reported with its own message; any other non-OK code is the firmware's refusal.
    pub fn set_rtc(&self) -> Result<(), TagError> {
        let mut inner = self.lock();
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default();
        if now.subsec_nanos() != 0 {
            sleep(Duration::from_nanos(u64::from(
                1_000_000_000 - now.subsec_nanos(),
            )));
        }
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default();

        if !inner.rpc(&request(req::Payload::SetRtc(now.as_millis() as i64))) {
            inner.ack = monitor_failure("Monitor call failed while setting RTC");
            return Err(inner.error());
        }
        match ack_err(&inner.ack) {
            ack::Err::Ok => Ok(()),
            ack::Err::Unspecified => {
                inner.ack.payload = Some(ack::Payload::ErrorMessage(
                    "Tag returned an empty or invalid RTC sync acknowledgement".into(),
                ));
                Err(inner.error())
            }
            _ => Err(inner.error()),
        }
    }

    /// Starts a self-test.
    ///
    /// Synchronous firmware replies on entering TEST, with `test_status`
    /// TEST_RUNNING; the test runs on the tag's next pass, and the tag
    /// returns to IDLE with the result. A test cut short by a reset leaves
    /// IDLE with TEST_RUNNING. The caller polls until IDLE.
    ///
    /// # Errors
    ///
    /// The transport failure or the firmware's refusal; see [`Tag::last_error`].
    pub fn test(&self, test: TestReq) -> Result<Option<Status>, TagError> {
        self.lock()
            .command(&request(req::Payload::Test(test as i32)))
    }

    // ---- Logs ----

    /// The block of state transitions starting at entry `index`. The number
    /// fetched is `states.len()`; an empty log means there are no more.
    pub fn state_log(&self, index: i32) -> Result<StateLog, TagError> {
        let mut inner = self.lock();
        match inner.payload(&log_request(log_req::LogDataType::SystemLog, index)) {
            Some(ack::Payload::SystemLog(log)) => Ok(log),
            _ => Err(inner.missing("state log")),
        }
    }

    /// The block of data-log entries starting at entry `index`, as the whole
    /// Ack, since its payload type depends on the tag. The Ack code is not
    /// checked; `Err` only when the RPC did not complete.
    pub fn data_log(&self, index: i32) -> Result<Ack, TagError> {
        self.request(&log_request(log_req::LogDataType::InternalData, index))
    }

    /// The calibration log, as the whole Ack. The Ack code is not checked;
    /// `Err` only when the RPC did not complete.
    pub fn calibration_log(&self) -> Result<Ack, TagError> {
        self.request(&request(req::Payload::Caldata(Empty {})))
    }

    // ---- Calibration constants ----

    /// Reads a stored set of calibration constants: `index < 0` for the last
    /// written, otherwise that entry.
    pub fn read_calibration(&self, index: i32) -> Result<CalibrationConstants, TagError> {
        let mut inner = self.lock();
        match inner.payload(&request(req::Payload::ReadCalibration(index))) {
            Some(ack::Payload::CalibrationConstants(constants)) => Ok(constants),
            _ => Err(inner.missing("calibration constants")),
        }
    }

    /// Appends a set of calibration constants to the tag's flash log.
    ///
    /// Ok only if the firmware answered OK. It refuses with INVAL when the
    /// magnetometer block is missing, and with PERM when the tag is not IDLE
    /// or its firmware has no calibration store (every tag but the CompassTag
    /// and IMUTag families).
    pub fn write_calibration(&self, constants: &CalibrationConstants) -> Result<(), TagError> {
        self.lock()
            .expect_ok(&request(req::Payload::WriteCalibration(*constants)))
    }
}

/// Detaches, so dropping a `Tag` ends the session cleanly.
impl Drop for Tag {
    fn drop(&mut self) {
        self.lock().monitor.detach();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ack(err: ack::Err, message: Option<&str>) -> Ack {
        Ack {
            err: err as i32,
            payload: message.map(|m| ack::Payload::ErrorMessage(m.into())),
        }
    }

    #[test]
    fn success_has_empty_message() {
        let e = describe(&ack(ack::Err::Ok, None), false);
        assert_eq!(e.err, ack::Err::Ok);
        assert!(!e.transport_failed);
        assert_eq!(e.message, "");
    }

    #[test]
    fn transport_failure_is_described() {
        let e = describe(&Ack::default(), true);
        assert!(e.transport_failed);
        assert_eq!(e.err, ack::Err::Unspecified);
        assert_eq!(e.message, "Monitor call failed");
    }

    #[test]
    fn firmware_refusal_names_the_code_unless_it_sent_a_message() {
        assert_eq!(
            describe(&ack(ack::Err::Perm, None), false).message,
            "Tag returned PERM"
        );
        assert_eq!(
            describe(&ack(ack::Err::Perm, Some("tag is running")), false).message,
            "tag is running"
        );
    }

    #[test]
    fn host_side_failures_record_monitor_code() {
        let e = describe(
            &monitor_failure("Monitor call failed while setting RTC"),
            true,
        );
        assert_eq!(e.err, ack::Err::Monitor);
        assert_eq!(e.message, "Monitor call failed while setting RTC");
    }

    #[test]
    fn log_requests_round_trip_through_protobuf() {
        use prost::Message;
        let req = log_request(log_req::LogDataType::SystemLog, 42);
        let decoded = Req::decode(req.encode_to_vec().as_slice()).unwrap();
        match decoded.payload {
            Some(req::Payload::Log(l)) => {
                assert_eq!(l.index, 42);
                assert_eq!(l.fmt, log_req::LogDataType::SystemLog as i32);
            }
            other => panic!("unexpected payload {other:?}"),
        }
    }

    #[test]
    fn detached_tag_fails_requests_as_transport_errors() {
        let tag = Tag::new();
        assert!(!tag.is_attached());
        assert_eq!(tag.git_sha(), None);
        let e = tag.status().unwrap_err();
        assert!(e.transport_failed);
        assert_eq!(e.message, "Monitor call failed");
        assert_eq!(tag.last_error(), e);
        assert_eq!(
            tag.debug_message(),
            "Tag returned an empty or invalid acknowledgement"
        );
    }

    /// An Ack with code `err` carrying a Status in `state`.
    fn status_ack(err: ack::Err, state: TagState) -> Ack {
        Ack {
            err: err as i32,
            payload: Some(ack::Payload::Status(Status {
                state: state as i32,
                ..Status::default()
            })),
        }
    }

    #[test]
    /// A synchronous reply yields the Status the command reached.
    fn command_reply_with_status_is_the_resulting_state() {
        let s = command_outcome(&status_ack(ack::Err::Ok, TagState::Finished))
            .unwrap()
            .unwrap();
        assert_eq!(s.state, TagState::Finished as i32);
    }

    #[test]
    /// A bare OK, from firmware that queues commands, yields no Status.
    fn command_reply_without_status_is_old_firmware() {
        assert_eq!(command_outcome(&ack(ack::Err::Ok, None)), Ok(None));
    }

    #[test]
    /// A refusal is an error whether or not it carries a Status, and names
    /// the tag's state when it does.
    fn command_refusal_is_an_error_naming_the_state() {
        let e = command_outcome(&status_ack(ack::Err::Perm, TagState::Idle)).unwrap_err();
        assert_eq!(e.err, ack::Err::Perm);
        assert_eq!(e.message, "Tag returned PERM (tag is IDLE)");
        let e = command_outcome(&ack(ack::Err::Perm, Some("not now"))).unwrap_err();
        assert_eq!(e.message, "not now");
    }
}
