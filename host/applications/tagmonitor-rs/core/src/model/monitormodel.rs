//! What Tag Monitor knows about the attached tag, and how each status read
//! changes it.
//!
//! Port of `host/applications/qtmon/core/monitormodel.h`. The engine reports
//! facts (attached, a status, a command sent); [`MonitorModel`] keeps them and
//! decides what they mean: the clock offset, whether a self-test or a start
//! has finished, how far an erase has got, and the tag's debug text as whole
//! lines. It never touches the tag.

use tagcore::proto::{Status, TagInfo, TagState, TagType, TestResult};

use super::commands::CommandInputs;
use super::tagrules::{
    DEFAULT_SECTOR_BYTES, clock_offset_seconds, erase_sector_maximum, rounded_sector_count,
};

/// How often the engine reads the attached tag's status, ms.
pub const STATUS_POLL_MS: u64 = 400;

/// A self-test result is accepted only from a status read at least this
/// long after Self-Test was sent, ms: the tag runs every test in one pass,
/// usually faster than a poll, and an earlier read still shows the previous
/// run's result.
pub const TEST_MINIMUM_MS: i64 = 1000;

/// Self-Test gives up waiting for a result after this long, ms.
pub const TEST_TIMEOUT_MS: i64 = 60_000;

/// The attached tag counts as not answering when no status has got through
/// for this long, ms: about seven missed polls.
pub const NO_ANSWER_MS: i64 = 3000;

/// Start is reported as failed when the tag has not reached RUNNING or
/// CONFIGURED this long after it was sent, ms. An acknowledgement is not a
/// completion, and a lost one is not a failure, so Start is judged by the
/// state the tag reaches.
pub const START_WAIT_MS: i64 = 7000;

/// Something a status read settled, for the UI to report.
#[derive(Debug, Clone, PartialEq)]
pub enum MonitorEvent {
    /// One whole line of the tag's debug text.
    TagLogLine(String),
    /// The self-test finished with this result.
    TestFinished(TestResult),
    /// No self-test result within [`TEST_TIMEOUT_MS`].
    TestTimedOut,
    /// The tag reached `state`; `message` is a start error it overcame, or
    /// empty.
    StartSucceeded {
        /// RUNNING or CONFIGURED.
        state: TagState,
        /// Tag::start's error, if it reported one.
        message: String,
    },
    /// The tag is still in `state` after [`START_WAIT_MS`]; `message` is the
    /// start error, or empty.
    StartFailed {
        /// The state the tag is in.
        state: TagState,
        /// Tag::start's error, if it reported one.
        message: String,
    },
    /// The tag returned to IDLE after an erase.
    EraseFinished,
}

/// The attached tag as Tag Monitor knows it: identity, latest status, and
/// the commands still waiting on the tag.
#[derive(Debug, Clone, Default)]
pub struct MonitorModel {
    /// A tag (live or fixture) is attached.
    attached: bool,
    /// The attached tag's identity.
    info: TagInfo,
    /// The latest status, once one has been read.
    status: Option<Status>,
    /// Host clock at the latest status, ms.
    status_ms: i64,
    /// Base-reported supply voltage at the latest status, volts.
    voltage: f32,
    /// Tag clock minus host clock at the latest status, seconds.
    clock_offset_s: f64,
    /// When Self-Test was sent, while its result is awaited.
    test_sent_ms: Option<i64>,
    /// When Start was sent, and Tag::start's error, while it is awaited.
    start_sent: Option<(i64, String)>,
    /// A download is running.
    downloading: bool,
    /// An erase was accepted and the tag has not yet returned to IDLE.
    erasing: bool,
    /// Sectors erased so far.
    erase_value: i32,
    /// Sectors the erase will cover; never decreases during one erase.
    erase_maximum: i32,
    /// Debug text received without its newline yet.
    debug_line: String,
}

impl MonitorModel {
    /// A model with no tag.
    pub fn new() -> Self {
        Self::default()
    }

    // ---- What the model knows ----

    /// A tag is attached.
    pub fn attached(&self) -> bool {
        self.attached
    }
    /// The attached tag's identity; empty when detached.
    pub fn info(&self) -> &TagInfo {
        &self.info
    }
    /// The latest status, if one has been read since attaching.
    pub fn status(&self) -> Option<&Status> {
        self.status.as_ref()
    }
    /// The tag's state at the latest status; STATE_UNSPECIFIED before one.
    pub fn state(&self) -> TagState {
        self.status
            .as_ref()
            .map_or(TagState::StateUnspecified, |s| s.state())
    }
    /// The attached tag's type.
    pub fn tag_type(&self) -> TagType {
        self.info.tag_type()
    }
    /// Base-reported supply voltage at the latest status, volts.
    pub fn voltage(&self) -> f32 {
        self.voltage
    }
    /// Tag clock minus host clock at the latest status, seconds.
    pub fn clock_offset_s(&self) -> f64 {
        self.clock_offset_s
    }
    /// The self-test result the latest status reported.
    pub fn last_test(&self) -> TestResult {
        self.status
            .as_ref()
            .map_or(TestResult::TestUnspecified, |s| s.test_status())
    }
    /// Self-Test was sent and its result has not been seen.
    pub fn test_pending(&self) -> bool {
        self.test_sent_ms.is_some()
    }
    /// Start was sent and the tag has not yet reached RUNNING or CONFIGURED.
    pub fn start_pending(&self) -> bool {
        self.start_sent.is_some()
    }
    /// A download is running.
    pub fn downloading(&self) -> bool {
        self.downloading
    }
    /// An erase was accepted and the tag has not yet returned to IDLE.
    pub fn erasing(&self) -> bool {
        self.erasing
    }
    /// Sectors erased so far, from the latest status.
    pub fn erase_value(&self) -> i32 {
        self.erase_value
    }
    /// Sectors the erase will cover; never decreases during one erase.
    pub fn erase_maximum(&self) -> i32 {
        self.erase_maximum
    }

    /// How long the attached tag has gone without a status getting through,
    /// ms: 0 when detached, before the first status, or while a download
    /// runs (it pauses the polls on purpose). [`NO_ANSWER_MS`] or more means
    /// the tag is not answering.
    pub fn silence_ms(&self, now_ms: i64) -> i64 {
        if !self.attached || self.status.is_none() || self.downloading {
            return 0;
        }
        (now_ms - self.status_ms).max(0)
    }

    /// The command rule's inputs, with the document's facts supplied by the
    /// caller.
    pub fn command_inputs(&self, document: bool, document_type: TagType) -> CommandInputs {
        CommandInputs {
            attached: self.attached,
            state: self.state(),
            tag_type: self.tag_type(),
            document,
            document_type,
            downloading: self.downloading,
            test_pending: self.test_pending(),
        }
    }

    // ---- What the engine reports ----

    /// A tag was attached: forgets everything about the previous one.
    pub fn set_attached(&mut self, info: TagInfo) {
        *self = MonitorModel {
            attached: true,
            info,
            ..MonitorModel::default()
        };
    }

    /// The tag was detached: forgets it, and every command waiting on it.
    pub fn set_detached(&mut self) {
        *self = MonitorModel::default();
    }

    /// Appends `text` to the debug-line buffer; returns each completed line.
    fn take_debug_lines(&mut self, text: &str) -> Vec<String> {
        // The firmware's text arrives in pieces across statuses; carriage
        // returns are dropped and an unfinished line waits for its newline.
        let mut lines = Vec::new();
        for ch in text.chars() {
            match ch {
                '\r' => {}
                '\n' => lines.push(std::mem::take(&mut self.debug_line)),
                c => self.debug_line.push(c),
            }
        }
        lines
    }

    /// One status read: records it, and returns what it settled, in order:
    /// one [`MonitorEvent::TagLogLine`] per whole line of debug text; a
    /// self-test result or timeout; a start's success or failure; an
    /// erase's end.
    ///
    /// `voltage` is the base-reported supply, volts; `host_ms` the host clock
    /// when the status was read, ms since the epoch.
    pub fn apply_status(
        &mut self,
        status: Status,
        voltage: f32,
        host_ms: i64,
    ) -> Vec<MonitorEvent> {
        let mut events: Vec<MonitorEvent> = self
            .take_debug_lines(&status.debug_message)
            .into_iter()
            .map(MonitorEvent::TagLogLine)
            .collect();

        self.status_ms = host_ms;
        self.voltage = voltage;
        self.clock_offset_s = clock_offset_seconds(status.millis, host_ms);

        if let Some(sent) = self.test_sent_ms {
            let elapsed = host_ms - sent;
            if elapsed >= TEST_MINIMUM_MS && status.test_status() != TestResult::TestRunning {
                self.test_sent_ms = None;
                events.push(MonitorEvent::TestFinished(status.test_status()));
            } else if elapsed > TEST_TIMEOUT_MS {
                self.test_sent_ms = None;
                events.push(MonitorEvent::TestTimedOut);
            }
        }

        if let Some((sent, message)) = &self.start_sent {
            let state = status.state();
            if matches!(state, TagState::Running | TagState::Configured) {
                events.push(MonitorEvent::StartSucceeded {
                    state,
                    message: message.clone(),
                });
                self.start_sent = None;
            } else if host_ms - sent >= START_WAIT_MS {
                events.push(MonitorEvent::StartFailed {
                    state,
                    message: message.clone(),
                });
                self.start_sent = None;
            }
        }

        if self.erasing {
            match status.state() {
                TagState::SReset => {
                    self.erase_value = status.sectors_erased;
                    self.erase_maximum = self.erase_maximum.max(erase_sector_maximum(
                        &status,
                        self.tag_type(),
                        self.info.extflashsz,
                        DEFAULT_SECTOR_BYTES,
                    ));
                }
                TagState::Idle => {
                    self.erasing = false;
                    events.push(MonitorEvent::EraseFinished);
                }
                _ => {}
            }
        }

        self.status = Some(status);
        events
    }

    // ---- What the UI sends ----

    /// Self-Test was sent at `now_ms`; its result is awaited.
    pub fn test_sent(&mut self, now_ms: i64) {
        self.test_sent_ms = Some(now_ms);
    }

    /// The tag refused Self-Test; nothing is awaited.
    pub fn test_refused(&mut self) {
        self.test_sent_ms = None;
    }

    /// Start was sent at `now_ms`; `call_message` is the tag's message if
    /// Tag::start reported failure, empty if it succeeded.
    pub fn start_sent(&mut self, now_ms: i64, call_message: String) {
        self.start_sent = Some((now_ms, call_message));
    }

    /// The tag accepted Erase: progress is tracked until it is IDLE.
    pub fn erase_sent(&mut self) {
        self.erasing = true;
        self.erase_value = 0;
        self.erase_maximum = match &self.status {
            Some(status) => erase_sector_maximum(
                status,
                self.tag_type(),
                self.info.extflashsz,
                DEFAULT_SECTOR_BYTES,
            ),
            None => rounded_sector_count(self.info.extflashsz, DEFAULT_SECTOR_BYTES),
        };
    }

    /// A download started.
    pub fn download_started(&mut self) {
        self.downloading = true;
    }

    /// The download ended, however it ended.
    pub fn download_ended(&mut self) {
        self.downloading = false;
    }
}
