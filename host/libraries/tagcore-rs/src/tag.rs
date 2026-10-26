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

use std::fmt;
use std::sync::{Mutex, MutexGuard};
use std::thread::sleep;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crate::linkadapt::{LinkAdaptStats, UsbDev};
use crate::proto::{
    Ack, CalibrationConstants, Config, Empty, LogReq, Req, StateLog, Status, TagInfo, TagState,
    TestReq, ack, log_req, req,
};
use crate::tagmonitor::{MemAccess, TagMonitor, TagMonitorStats, TargetFamily};

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

/// The firmware's Ack code, treating an out-of-range value as `Unspecified`.
fn ack_err(ack: &Ack) -> ack::Err {
    ack::Err::try_from(ack.err).unwrap_or(ack::Err::Unspecified)
}

/// The outcome of an RPC, as `Tag::LastError()` reports it: the firmware's
/// message if it sent one, otherwise one derived from the transport flag and
/// the Ack code; empty for a success.
fn describe(ack: &Ack, rpc_failed: bool) -> TagError {
    let err = ack_err(ack);
    let mut message = ack.error_message().to_owned();
    if message.is_empty() {
        if rpc_failed {
            message = "Monitor call failed".into();
        } else if err != ack::Err::Ok {
            message = format!("Tag returned {}", err.as_str_name());
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

fn request(payload: req::Payload) -> Req {
    Req {
        payload: Some(payload),
    }
}

fn log_request(fmt: log_req::LogDataType, index: i32) -> Req {
    request(req::Payload::Log(LogReq {
        fmt: fmt as i32,
        index: index as u32,
    }))
}

/// State behind the [`Tag`] lock.
struct Inner {
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
pub struct Tag {
    inner: Mutex<Inner>,
}

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
    pub fn count_bittag_log_headers(&self) -> Option<usize> {
        self.lock().monitor.count_bittag_log_headers()
    }

    /// Reads an old BitTag's records straight from flash as a data-log Ack;
    /// see [`TagMonitor::read_bittag_log_from_flash`].
    pub fn bittag_log_from_flash(&self, index: i32) -> Option<Ack> {
        self.lock().monitor.read_bittag_log_from_flash(index)
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
            err => format!("Tag returned {}", err.as_str_name()),
        }
    }

    // ---- Tag control ----

    /// Erases the stored data and returns the tag to IDLE.
    pub fn erase(&self) -> Result<(), TagError> {
        self.lock()
            .expect_ok(&request(req::Payload::Erase(Empty {})))
    }

    /// Configures the tag with `config` and starts it.
    pub fn start(&self, config: &Config) -> Result<(), TagError> {
        let mut inner = self.lock();
        if !inner.rpc(&request(req::Payload::Start(config.clone()))) {
            inner.ack = monitor_failure("Monitor call failed");
            return Err(inner.error());
        }
        if ack_err(&inner.ack) == ack::Err::Ok {
            Ok(())
        } else {
            Err(inner.error())
        }
    }

    /// Stops a running tag.
    pub fn stop(&self) -> Result<(), TagError> {
        self.lock()
            .expect_ok(&request(req::Payload::Stop(Empty {})))
    }

    /// Puts the tag in its sensor-calibration state.
    pub fn calibrate(&self) -> Result<(), TagError> {
        self.lock()
            .expect_ok(&request(req::Payload::Calibrate(Empty {})))
    }

    /// Sets the tag's clock to the host's.
    ///
    /// Waits for the next whole second of system time, then sends the time
    /// in milliseconds since the Unix epoch, so the tag's clock lands close
    /// to a second boundary. Blocks for up to one second.
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

    /// Runs a self-test.
    pub fn test(&self, test: TestReq) -> Result<(), TagError> {
        self.lock()
            .expect_ok(&request(req::Payload::Test(test as i32)))
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

    /// Stores a set of calibration constants.
    ///
    /// As in the C++ class, success means the RPC completed: the Ack code is
    /// not checked, so inspect [`last_error`](Self::last_error) to see
    /// whether the firmware accepted them.
    pub fn write_calibration(&self, constants: &CalibrationConstants) -> Result<(), TagError> {
        let mut inner = self.lock();
        if inner.rpc(&request(req::Payload::WriteCalibration(*constants))) {
            Ok(())
        } else {
            Err(inner.error())
        }
    }
}

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
}
