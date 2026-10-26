//! The programming session: qtprogram's `ProgramSession`
//! (`programsession.cpp`), with `Station::program`, `Station::retest` and
//! `Station::writeRecord` (`station.cpp`), ported.
//!
//! # Program
//!
//! A program session stops the tag through its monitor and, if it holds a
//! log (FINISHED or ABORTED), has the tag's own firmware erase it -- the
//! firmware that wrote a log is the one that knows its layout. Once the tag
//! is IDLE it detaches and programs with probe-rs on a worker thread
//! ([`Step::Programming`]), reattaches ([`Step::Connecting`], which resets
//! the tag), runs the self-tests ([`Step::Testing`]) and records a row. A
//! blank board (no firmware answers) goes straight to programming.
//!
//! The session deliberately does **not** start the tag at the end, unlike
//! the sequence in `design/qtprogram-spec.md`: a start belongs to the tag's
//! deployment, and on a PresTag a SetRtc after a start hangs the firmware
//! (`embedded/tags/TODO.md`). The tag is left IDLE.
//!
//! # Retest
//!
//! A retest programs nothing. If the tag is IDLE and an image with the
//! tag's commit is at hand, it reads the flash back on a worker
//! ([`Step::Reading`]) and compares it with the image, then reconnects,
//! runs the self-tests and records a row.
//!
//! # Driving
//!
//! The session's state is a [`SessionState`] in `Station::session`; its
//! steps are methods on [`Station`], all on the engine thread. The waiting
//! steps (Stopping, Erasing, Connecting, Testing) are driven by a 1 s tick,
//! as qtprogram's were: `Station::fire_timers` calls
//! [`Station::session_tick`] when `tick_at` is due. Programming and Reading
//! have no tick: they end when their worker's `Msg::Done` arrives.
//! PowerCycle waits for the user's Retry or Give Up. Each tick logs one
//! DEBUG line ([`Station::log_tick`]).
//!
//! [`Station::finish`] ends every session, successful or not, and writes the
//! record row. See `design/engine.md`.

use std::path::Path;
use std::time::{Duration, Instant};

use chrono::Utc;
use log::{debug, error, info, warn};
use serde_json::json;
use tagcore::proto::{TagState, TagType, TestReq, TestResult};
use tagprobe::Progress;

use super::Msg;
use super::station::{Check, Station, state_name, test_name};
use crate::model::flashplan::Erase;
use crate::model::image::{FirmwareImage, compare_image_flash, image_flash_range, same_commit};
use crate::model::record::ProgramRecord;
use crate::model::settings as key;

/// Ticks (seconds) the tag may take to reach IDLE after the stop before
/// the session fails.
const STOP_TICKS: u32 = 30;
/// Ticks (seconds) of reattach attempts after programming or a read-back
/// before the session asks the user to power-cycle the tag.
const CONNECT_TICKS: u32 = 6;
/// Ticks (seconds) the self-tests may run before the session fails with
/// TEST_FAILED.
const TEST_TICKS: u32 = 120;
/// Bytes per IMUTag log entry (`external_data_count` counts these), for
/// the erase progress fallback.
const IMU_LOG_PAGE: i64 = 2048;
/// Bytes per external-flash erase sector, for the erase progress
/// fallback.
const SECTOR: i64 = 4096;
/// The session tick period.
const TICK: Duration = Duration::from_secs(1);
/// What the record's `host_version` column says wrote the row: the host
/// tools package version when CMake builds the app (qtprogram records the
/// same number), else the crate's. The `tagprogram-rs` prefix tells this
/// port's rows from qtprogram's.
fn host_version() -> String {
    let version = option_env!("TAGPROGRAM_HOST_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"));
    format!("tagprogram-rs {version}")
}

/// Bytes as whole external-flash sectors, rounded up; 0 for a negative
/// count.
fn sectors(bytes: i64) -> i64 {
    (bytes.max(0) + SECTOR - 1) / SECTOR
}

/// The session's steps (`ProgramSession::Step`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Step {
    /// Stopping the tag and waiting for IDLE (ticked); erases a held log on
    /// the way.
    Stopping,
    /// The tag's firmware is erasing its log (SRESET); ticked, with sector
    /// progress.
    Erasing,
    /// probe-rs is erasing, programming and verifying on a worker; no tick.
    /// Cannot be cancelled.
    Programming,
    /// Retest: probe-rs is reading the flash back on a worker; no tick.
    Reading,
    /// Reattaching to the tag after programming or a read-back (ticked).
    Connecting,
    /// The tag did not answer: waiting for the user to replug the base and
    /// press Retry, or Give Up. No tick.
    PowerCycle,
    /// The self-tests are running (ticked).
    Testing,
    /// Finished; the session is kept for its message.
    Done,
}

/// What a retest learned about the flash.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum FlashCheck {
    /// Not read (a program session, or a retest that never reached it).
    NotChecked,
    /// A read was wanted but not done or failed; why.
    Unread(String),
    /// The flash read back matches the image.
    Matches,
    /// The flash differs from the image; how. Makes the retest a MISMATCH.
    Differs(String),
}

/// One session's state, kept in `Station::session`.
///
/// Created by [`Station::program`] or [`Station::retest`]; kept after the
/// session ends, with `running` false, so its message stays on the step line
/// until Next Tag.
pub(crate) struct SessionState {
    /// The session has not finished.
    pub running: bool,
    /// The current step.
    pub step: Step,
    /// The step line's text; the final message once finished.
    pub step_text: String,
    /// `None`: the busy indicator; `Some((done, total))` otherwise.
    pub progress: Option<(u64, u64)>,
    /// The next tick, while the timer runs.
    pub tick_at: Option<Instant>,
    /// Ticks spent in the current waiting step.
    ticks: u32,
    /// A retest rather than a program session.
    retest: bool,
    /// The programmer was started. A program session that ends before this
    /// (cancelled, or failed while stopping) writes no record row.
    programmed: bool,
    /// After programming, the tag reports a commit other than the image's.
    commit_mismatch: bool,
    /// The label the user gave, trimmed.
    label: String,
    /// The image to program, or for a retest the image with the tag's commit
    /// (`None` when none is at hand).
    image: Option<FirmwareImage>,
    /// The target voltage at the connect check, for the record.
    voltage: Option<f32>,
    /// The base's ST-LINK serial, for the record.
    serial: String,
    /// The tag's UUID: from before the session, then as the tag reports it
    /// after programming (a blank board gets one only then).
    uuid: String,
    /// The commit the tag reports after programming or for the retest.
    git_hash: String,
    /// The self-test result's name, once the tests finish.
    test_status: String,
    /// The retest's flash comparison.
    flash: FlashCheck,
    /// The record's result: OK, TEST_FAILED, MISMATCH, PROGRAM_FAILED: ...,
    /// NO_ANSWER, CANCELLED or FAILED: ...; empty until finished.
    pub result: String,
    /// The final message for the user; empty until finished.
    pub message: String,
}

impl SessionState {
    /// A running session at [`Step::Stopping`] with nothing learned yet; the
    /// callers fill in the voltage, serial and UUID.
    fn new(label: &str, image: Option<FirmwareImage>, retest: bool) -> Self {
        Self {
            running: true,
            step: Step::Stopping,
            step_text: String::new(),
            progress: None,
            tick_at: None,
            ticks: 0,
            retest,
            programmed: false,
            commit_mismatch: false,
            label: label.to_owned(),
            image,
            voltage: None,
            serial: String::new(),
            uuid: String::new(),
            git_hash: String::new(),
            test_status: String::new(),
            flash: FlashCheck::NotChecked,
            result: String::new(),
            message: String::new(),
        }
    }

    /// Cancel is offered: power-cycle waits, and the steps before
    /// programming.
    ///
    /// Not while the programmer runs: an interrupted flash write leaves a tag
    /// worse off than a wait does, so Cancel is hidden then and
    /// [`Station::session_cancel`] refuses.
    pub(crate) fn cancellable(&self) -> bool {
        self.running && matches!(self.step, Step::PowerCycle | Step::Stopping | Step::Erasing)
    }
}

impl Station {
    /// The current session.
    ///
    /// # Panics
    ///
    /// Panics if there is none; every caller runs inside a session.
    fn sess(&mut self) -> &mut SessionState {
        self.session.as_mut().expect("a session")
    }

    /// Moves to `step`, logs `text` at INFO and shows it on the step line,
    /// with the busy indicator in place of any progress.
    fn set_step(&mut self, step: Step, text: impl Into<String>) {
        let text = text.into();
        info!("{text}");
        let s = self.sess();
        s.step = step;
        s.step_text = text;
        s.progress = None;
    }

    /// Starts the 1 s tick for a waiting step, resetting its tick count.
    fn start_timer(&mut self) {
        let s = self.sess();
        s.ticks = 0;
        s.tick_at = Some(Instant::now() + TICK);
    }

    /// Stops the tick.
    fn stop_timer(&mut self) {
        self.sess().tick_at = None;
    }

    // ---- starting ----------------------------------------------------

    /// qtprogram's `Station::program`: after the Program dialog.
    ///
    /// Re-checks the record (it may have been opened elsewhere since), then
    /// alerts and returns if [`Station::can_program`] refuses. Otherwise saves
    /// the label, stops the status poll and starts a session: at
    /// [`Step::Stopping`] for an attached tag, or straight to the programmer
    /// for a blank board. The UI has already confirmed any label conflict.
    pub(crate) fn program(&mut self, label: &str) {
        self.check_record();
        if let Err(why) = self.can_program() {
            self.alert("Program Tag", &why);
            return;
        }
        let label = label.trim().to_owned();
        self.last_label = label.clone();
        self.save_setting(key::LAST_LABEL, label.clone());
        self.result = (String::new(), None);
        self.stop_status_poll();
        let mut s = SessionState::new(&label, self.image.clone(), false);
        s.voltage = self.connect.as_ref().and_then(|c| c.voltage);
        s.serial = self
            .base
            .as_ref()
            .map(|b| b.serial.clone())
            .unwrap_or_default();
        s.uuid = self
            .info
            .as_ref()
            .map(|i| i.uuid.clone())
            .unwrap_or_default();
        self.session = Some(s);
        if !self.attached() {
            self.run_programmer();
            return;
        }
        self.set_step(Step::Stopping, "Stopping the tag");
        self.start_timer();
    }

    /// qtprogram's `Station::retest`.
    ///
    /// Refused, with an alert saying why, while a session runs or the record
    /// is not OK. Picks the
    /// image with the tag's commit ([`Station::retest_image`]) and starts a
    /// retest session. A tag not attached is reattached first -- which resets
    /// it -- and the session ends FAILED, with a row, if it does not answer.
    /// Without an image, or when the tag is not IDLE (or its status cannot be
    /// read), the flash is not read and the tests start at once; otherwise
    /// the read-back starts.
    pub(crate) fn retest(&mut self, label: &str) {
        self.check_record();
        if self.session_running() {
            self.alert("Retest Tag", "Programming is in progress.");
            return;
        }
        if self.level(Check::Record) != super::station::Level::Ok {
            let why = self.text(Check::Record).to_owned();
            self.alert("Retest Tag", &why);
            return;
        }
        let label = label.trim().to_owned();
        self.last_label = label.clone();
        self.save_setting(key::LAST_LABEL, label.clone());
        self.result = (String::new(), None);
        self.stop_status_poll();
        let image = self.retest_image();
        let mut s = SessionState::new(&label, image, true);
        s.voltage = self.connect.as_ref().and_then(|c| c.voltage);
        s.serial = self
            .base
            .as_ref()
            .map(|b| b.serial.clone())
            .unwrap_or_default();
        s.uuid = self
            .info
            .as_ref()
            .map(|i| i.uuid.clone())
            .unwrap_or_default();
        self.session = Some(s);

        if !self.attached() && !self.reattach() {
            self.finish(
                "FAILED: the tag does not answer",
                "The tag does not answer, so it cannot be tested.",
            );
            return;
        }
        if self.sess().image.is_none() {
            self.begin_tests();
            return;
        }
        let idle = self
            .tag
            .status()
            .is_ok_and(|st| st.state == TagState::Idle as i32);
        if !idle {
            self.sess().flash =
                FlashCheck::Unread("the tag was not IDLE, so its flash was not read".into());
            self.begin_tests();
            return;
        }
        self.read_flash();
    }

    /// The image whose commit the tag runs: the chosen one if it matches,
    /// else the first cached release, in catalog order, with the tag's target
    /// and commit. `None` for a tag that does not answer or when none matches.
    ///
    /// Loads and hashes each candidate image on the engine thread.
    fn retest_image(&self) -> Option<FirmwareImage> {
        let tag_hash = self.info.as_ref()?.githash.clone();
        let commit = |i: &FirmwareImage| {
            if i.identity.git_hash.is_empty() {
                i.manifest_commit.clone()
            } else {
                i.identity.git_hash.clone()
            }
        };
        if let Some(image) = &self.image
            && image.usable()
            && same_commit(&commit(image), &tag_hash)
        {
            return Some(image.clone());
        }
        let target = self.tag_target()?;
        self.catalog
            .releases()
            .iter()
            .filter(|r| self.catalog.is_cached(&r.tag))
            .filter(|r| self.catalog.targets(&r.tag).contains(&target))
            .map(|r| {
                crate::model::image::load_firmware_image(
                    &self.catalog.image_path(&r.tag, &target),
                    Some(&r.tag),
                )
            })
            .find(|i| i.usable() && same_commit(&commit(i), &tag_hash))
    }

    // ---- the tick ----------------------------------------------------

    /// One DEBUG line per tick: the step and what the tag reported -- its
    /// state, erase sectors done/total and test result -- or that it did not
    /// answer.
    fn log_tick(step: Step, status: &Result<tagcore::proto::Status, tagcore::TagError>) {
        match status {
            Ok(st) => debug!(
                "tick {step:?}: {} sectors {}/{} test {}",
                state_name(st.state),
                st.sectors_erased,
                st.erase_sectors_total_plus_one - 1,
                test_name(st.test_status)
            ),
            Err(e) => debug!("tick {step:?}: no answer ({e})"),
        }
    }

    /// One tick of a running session, from `Station::fire_timers`: runs the
    /// waiting step's handler. Steps without a tick are ignored.
    pub(crate) fn session_tick(&mut self) {
        let Some(step) = self.session.as_ref().filter(|s| s.running).map(|s| s.step) else {
            return;
        };
        match step {
            Step::Stopping => self.tick_stopping(),
            Step::Erasing => self.tick_erasing(),
            Step::Connecting => self.tick_connecting(),
            Step::Testing => self.tick_testing(),
            _ => {}
        }
    }

    /// Stopping: brings the tag to IDLE.
    ///
    /// IDLE starts the programmer. FINISHED or ABORTED: asks the tag's own
    /// firmware to erase its log and moves to Erasing. Any other state: sends
    /// one stop on the first tick (not during TEST or SRESET, which end on
    /// their own) and waits up to [`STOP_TICKS`]. A tag that stops answering
    /// or refuses the erase fails the session.
    fn tick_stopping(&mut self) {
        let status = self.tag.status();
        Self::log_tick(Step::Stopping, &status);
        let Ok(status) = status else {
            self.finish(
                "FAILED: the tag stopped answering",
                "The tag stopped answering before it was programmed.",
            );
            return;
        };
        let state = TagState::try_from(status.state).unwrap_or(TagState::StateUnspecified);
        match state {
            TagState::Aborted | TagState::Finished => {
                if self.tag.erase().is_err() {
                    self.finish(
                        "FAILED: the tag refused the erase",
                        "The tag refused to erase its log.",
                    );
                    return;
                }
                self.sess().ticks = 0;
                self.set_step(Step::Erasing, "The tag is erasing its log");
            }
            TagState::Idle => self.run_programmer(),
            _ => {
                if self.sess().ticks == 0 && !matches!(state, TagState::Test | TagState::SReset) {
                    let _ = self.tag.stop();
                }
                self.sess().ticks += 1;
                if self.sess().ticks > STOP_TICKS {
                    let name = state_name(status.state);
                    self.finish(
                        &format!("FAILED: the tag stayed {name}"),
                        &format!("The tag stayed {name}. Stop and erase it in Tag Monitor, then program again."),
                    );
                }
            }
        }
    }

    /// Erasing: shows the tag's erase progress until it is IDLE again, then
    /// returns to Stopping (without a new step line) and ticks it at once.
    ///
    /// The progress total is what the tag reports; a tag that reports 0 falls
    /// back to its log size (IMUTag) or its external flash size, and with no
    /// total at all the bar is the busy indicator. Any state but SRESET,
    /// FINISHED, ABORTED or IDLE -- or no answer -- fails the session.
    fn tick_erasing(&mut self) {
        let status = self.tag.status();
        Self::log_tick(Step::Erasing, &status);
        let status = status.ok();
        let state = status
            .as_ref()
            .and_then(|s| TagState::try_from(s.state).ok());
        match (state, &status) {
            (Some(TagState::SReset), Some(st)) => {
                // As qtprogram: a total of zero (none reported) falls back
                // to the log's or the flash's size, and a bar with no total
                // is the busy indicator.
                let reported = i64::from(st.erase_sectors_total_plus_one.max(1) - 1);
                let total = if reported > 0 {
                    reported
                } else {
                    self.info.as_ref().map_or(0, |info| {
                        if info.tag_type == TagType::Imutag as i32 {
                            sectors(i64::from(st.external_data_count) * IMU_LOG_PAGE)
                        } else {
                            sectors(info.extflashsz)
                        }
                    })
                };
                self.sess().progress =
                    (total > 0).then(|| (st.sectors_erased.max(0) as u64, total as u64));
            }
            (Some(TagState::Aborted | TagState::Finished), _) => {}
            (Some(TagState::Idle), _) => {
                // Back to Stopping without a step change, and straight on.
                let s = self.sess();
                s.step = Step::Stopping;
                s.ticks = 0;
                self.tick_stopping();
            }
            _ => {
                let now = status.map_or("not answering".to_owned(), |s| state_name(s.state));
                self.finish(
                    "FAILED: no IDLE after the erase",
                    &format!("The tag did not return to IDLE after its erase (it is {now})."),
                );
            }
        }
    }

    /// Connecting: reattaches until the firmware answers, then runs the
    /// self-tests. After [`CONNECT_TICKS`] tries it asks the user to replug the
    /// base and press Retry ([`Step::PowerCycle`]); a mass erase can leave the
    /// part in its bootloader until power is removed. Each try resets the
    /// tag.
    fn tick_connecting(&mut self) {
        if self.reattach_tag() {
            self.stop_timer();
            self.begin_tests();
            return;
        }
        self.sess().ticks += 1;
        if self.sess().ticks < CONNECT_TICKS {
            return;
        }
        self.stop_timer();
        let s = self.sess();
        let text = if s.retest {
            "The tag does not answer after its flash was read. Unplug the base, plug it in again, and press Retry."
        } else if s.image.as_ref().is_some_and(|i| i.plan.erase == Erase::All) {
            "Programmed, but the tag does not answer. A mass erase can leave it in its bootloader until it is powered off: unplug the base, plug it in again, and press Retry."
        } else {
            "Programmed, but the tag does not answer. Unplug the base, plug it in again, and press Retry."
        };
        self.set_step(Step::PowerCycle, text);
    }

    /// Attached with the firmware answering `TagInfo`, attaching if need be
    /// (which resets the tag).
    fn reattach_tag(&mut self) -> bool {
        if self.attached() && self.tag.tag_info().is_ok() {
            return true;
        }
        self.reattach() && self.tag.tag_info().is_ok()
    }

    /// Testing: waits for the tag to leave TEST, up to [`TEST_TICKS`], then
    /// sets its clock and finishes. A program session ends MISMATCH if the tag
    /// runs another commit, OK on ALL_PASSED, else TEST_FAILED; the tag is not
    /// started. A retest ends in [`Station::finish_retest`].
    fn tick_testing(&mut self) {
        let status = self.tag.status();
        Self::log_tick(Step::Testing, &status);
        let Ok(status) = status else {
            self.finish(
                "FAILED: the tag stopped answering",
                "The tag stopped answering during its self-tests.",
            );
            return;
        };
        if status.state == TagState::Test as i32 {
            self.sess().ticks += 1;
            if self.sess().ticks > TEST_TICKS {
                self.finish("TEST_FAILED", "The self-tests did not finish.");
            }
            return;
        }
        let tests = test_name(status.test_status);
        self.sess().test_status = tests.clone();
        let _ = self.tag.set_rtc();
        if self.sess().retest {
            self.finish_retest(&tests);
            return;
        }
        // Done. The programmer does not start the tag: a start belongs to
        // its deployment, and on a PresTag it leaves the RTC write-protected
        // until the next reset, so a later SetRtc hangs the firmware
        // (embedded/tags/TODO.md). The tag stays IDLE.
        let s = self.sess();
        let (result, message) = if s.commit_mismatch {
            (
                "MISMATCH".to_owned(),
                format!(
                    "Programmed, but the tag reports commit {}, which is not the image's. It is not running the image just written.",
                    s.git_hash
                ),
            )
        } else if tests == TestResult::AllPassed.as_str_name() {
            (
                "OK".to_owned(),
                "Programmed and tested: ALL_PASSED. The tag is IDLE.".to_owned(),
            )
        } else {
            (
                "TEST_FAILED".to_owned(),
                format!("Programmed, but the self-test reported {tests}."),
            )
        };
        self.finish(&result, &message);
    }

    /// Ends a retest: MISMATCH when the flash differs from the image, else OK
    /// on ALL_PASSED, else TEST_FAILED, with a message describing the flash
    /// comparison.
    fn finish_retest(&mut self, tests: &str) {
        let s = self.sess();
        let name = s
            .image
            .as_ref()
            .map(|i| i.display_name())
            .unwrap_or_default();
        let result = if matches!(s.flash, FlashCheck::Differs(_)) {
            "MISMATCH"
        } else if tests == TestResult::AllPassed.as_str_name() {
            "OK"
        } else {
            "TEST_FAILED"
        };
        let flash = match &s.flash {
            _ if s.image.is_none() => {
                " No downloaded image matches the tag's commit, so its flash was not compared."
                    .to_owned()
            }
            FlashCheck::Matches => format!(" The flash matches {name}."),
            FlashCheck::Differs(note) => format!(" The flash is not {name}: {note}"),
            FlashCheck::Unread(note) => format!(" The flash was not compared: {note}."),
            FlashCheck::NotChecked => " The flash was not compared.".to_owned(),
        };
        self.finish(result, &format!("Self-tests: {tests}.{flash}"));
    }

    // ---- programming and reading -------------------------------------

    /// Programs the tag: qtprogram's `runProgrammer`, with probe-rs.
    ///
    /// Stops the tick, detaches the monitor and marks the session programmed.
    /// Finds the base and the probe-rs target (failing the session if either
    /// cannot be had), then runs `tagprobe::program` on a worker with
    /// `Station::worker_busy` set: erase as the image's flash plan says,
    /// program, verify and start the image. Progress comes back as `Msg::Done`
    /// closures ([`Station::program_progress`]). Success moves to Connecting;
    /// an error ends PROGRAM_FAILED.
    ///
    /// # Panics
    ///
    /// Panics if the session has no image; [`Station::program`] only starts one
    /// with a usable image.
    fn run_programmer(&mut self) {
        self.stop_timer();
        self.tag.detach();
        let image = self.sess().image.clone().expect("an image to program");
        self.sess().programmed = true;
        self.set_step(
            Step::Programming,
            format!(
                "Programming {}: {}",
                image.display_name(),
                image.plan.summary()
            ),
        );
        let Some(base) = self.find_base() else {
            self.finish(
                "PROGRAM_FAILED: the base is not on USB",
                "Programming failed (the base is not on USB).",
            );
            return;
        };
        let chip = match self.chip(&image) {
            Ok(c) => c,
            Err(e) => {
                self.finish(
                    &format!("PROGRAM_FAILED: {e}"),
                    &format!("Programming failed ({e})."),
                );
                return;
            }
        };
        let driver = self.driver();
        let erase = self.probe_erase(&image);
        info!("probe-rs: {chip}, {}, erase {erase:?}", driver.name());
        let tx = self.sender();
        self.worker_busy = true;
        self.spawn_work(
            move || {
                tagprobe::program(&base, driver, chip, &image.elf_path, erase, |p| {
                    let _ = tx.send(Msg::Done(Box::new(move |s: &mut Station| {
                        s.program_progress(p)
                    })));
                })
            },
            |s, result| {
                s.worker_busy = false;
                match result {
                    Err(e) => {
                        error!("{e}");
                        s.finish(
                            &format!("PROGRAM_FAILED: {e}"),
                            &format!(
                                "Programming failed ({e}). The probe's messages are in the log."
                            ),
                        );
                    }
                    Ok(()) => {
                        s.set_step(Step::Connecting, "Connecting to the new image");
                        s.start_timer();
                    }
                }
            },
        );
    }

    /// Shows probe-rs progress: a named step goes to the log with the busy
    /// indicator, bytes to the progress bar. Ignored once the session has
    /// ended.
    fn program_progress(&mut self, p: Progress) {
        let Some(s) = self.session.as_mut().filter(|s| s.running) else {
            return;
        };
        match p {
            Progress::Step(name) => {
                info!("probe-rs: {name}");
                s.progress = None;
            }
            Progress::Bytes { done, total } => s.progress = Some((done, total)),
        }
    }

    /// Retest: reads the image's flash range back on a worker with
    /// `tagprobe::read_memory` and compares it with the image, then moves to
    /// Connecting. The monitor is detached first; reconnecting afterwards
    /// resets the tag. If the range, base or part cannot be had, the flash is
    /// marked unread and the tests start at once.
    ///
    /// # Panics
    ///
    /// Panics if the session has no image; [`Station::retest`] checks first.
    fn read_flash(&mut self) {
        let image = self.sess().image.clone().expect("an image to compare");
        let (start, len) = match image_flash_range(&image.elf_path) {
            Ok(r) => r,
            Err(e) => {
                self.sess().flash = FlashCheck::Unread(e);
                self.begin_tests();
                return;
            }
        };
        let (Some(base), Ok(chip)) = (self.find_base(), self.chip(&image)) else {
            self.sess().flash = FlashCheck::Unread("the base or the part is unknown".into());
            self.begin_tests();
            return;
        };
        self.tag.detach();
        self.set_step(
            Step::Reading,
            format!(
                "Reading the tag's flash to compare with {}",
                image.display_name()
            ),
        );
        let driver = self.driver();
        self.worker_busy = true;
        let elf = image.elf_path.clone();
        self.spawn_work(
            move || tagprobe::read_memory(&base, driver, chip, u64::from(start), len as usize),
            move |s, result| {
                s.worker_busy = false;
                let check = match result {
                    Err(e) => FlashCheck::Unread(format!("probe-rs could not read it ({e})")),
                    Ok(bytes) => match compare_image_flash(&elf, start, &bytes) {
                        None => FlashCheck::Matches,
                        Some(note) => {
                            warn!("{note}");
                            FlashCheck::Differs(note)
                        }
                    },
                };
                s.sess().flash = check;
                s.set_step(Step::Connecting, "Connecting to the tag again");
                s.start_timer();
            },
        );
    }

    /// Starts the self-tests: records the tag's UUID and commit (and, after
    /// programming, whether the commit is the image's), sets the tag's clock,
    /// requests every test and moves to Testing. A refused test request is
    /// logged; the Testing tick then sees the tag not in TEST and finishes with
    /// whatever result it reports.
    fn begin_tests(&mut self) {
        if let Ok(info) = self.tag.tag_info() {
            let s = self.sess();
            s.uuid = info.uuid.clone();
            s.git_hash = info.githash.clone();
            if !s.retest {
                let expected = s.image.as_ref().map_or(String::new(), |i| {
                    if i.identity.git_hash.is_empty() {
                        i.manifest_commit.clone()
                    } else {
                        i.identity.git_hash.clone()
                    }
                });
                s.commit_mismatch = !expected.is_empty() && !same_commit(&expected, &info.githash);
                if s.commit_mismatch {
                    warn!(
                        "The tag reports commit {}, and the image is {expected}.",
                        info.githash
                    );
                }
            }
        }
        let _ = self.tag.set_rtc();
        if let Err(e) = self.tag.test(TestReq::RunAll) {
            warn!("The self-test request failed: {e}");
        }
        self.set_step(Step::Testing, "Running the self-tests");
        self.start_timer();
    }

    // ---- the user's buttons ------------------------------------------

    /// Retry, at [`Step::PowerCycle`]: back to Connecting for another
    /// [`CONNECT_TICKS`] tries. Ignored at any other step.
    pub(crate) fn session_retry_connect(&mut self) {
        if self
            .session
            .as_ref()
            .is_some_and(|s| s.running && s.step == Step::PowerCycle)
        {
            let text = if self.sess().retest {
                "Connecting to the tag"
            } else {
                "Connecting to the new image"
            };
            self.set_step(Step::Connecting, text);
            self.start_timer();
        }
    }

    /// Cancel or Give Up, when [`SessionState::cancellable`]: at PowerCycle it
    /// ends NO_ANSWER; before programming, CANCELLED (no row is written for a
    /// program session that never programmed). Ignored otherwise.
    ///
    /// Cancelling while Stopping or Erasing leaves the tag where it is: an
    /// erase the tag's firmware has begun runs to its end.
    pub(crate) fn session_cancel(&mut self) {
        let Some(s) = self.session.as_ref().filter(|s| s.cancellable()) else {
            return;
        };
        if s.step == Step::PowerCycle && s.retest {
            self.finish(
                "NO_ANSWER",
                "The tag never answered after its flash was read. Power-cycle it and press Attach; if it still does not answer, program it again.",
            );
        } else if s.step == Step::PowerCycle {
            self.finish(
                "NO_ANSWER",
                "Programmed, but the tag never answered. Power-cycle it and press Attach; if it still does not answer, program it again.",
            );
        } else {
            self.finish("CANCELLED", "Cancelled before programming.");
        }
    }

    // ---- the end -----------------------------------------------------

    /// Ends the session with `result` and `message`: stops the tick, shows
    /// the message on the step line, and writes the record
    /// ([`Station::write_record`]).
    pub(crate) fn finish(&mut self, result: &str, message: &str) {
        info!("{message}");
        let s = self.sess();
        s.tick_at = None;
        s.running = false;
        s.step = Step::Done;
        s.step_text = message.to_owned();
        s.progress = None;
        s.result = result.to_owned();
        s.message = message.to_owned();
        self.write_record();
    }

    /// qtprogram's `Station::writeRecord`.
    ///
    /// First reattaches if need be -- resetting the tag -- to refresh its info
    /// and status, and resumes the status poll. A program session that never
    /// reached the programmer writes no row: it alerts with its message. Otherwise
    /// it appends a row (for a retest, image fields only when the flash was
    /// compared), counts it in the batch, sets the result line, and alerts for
    /// anything but a written OK. A row that cannot be written is logged in full
    /// at ERROR, so it is not lost.
    fn write_record(&mut self) {
        let prior_uuid = self
            .info
            .as_ref()
            .map(|i| i.uuid.clone())
            .unwrap_or_default();
        if (self.attached() || self.reattach())
            && let Ok(info) = self.tag.tag_info()
        {
            self.info = Some(info);
            self.read_status();
            self.check_tag_state();
            self.resume_status_poll();
        }
        let s = self.session.as_ref().expect("a session");
        let mut message = s.message.clone();
        if !s.programmed && !s.retest {
            self.result = (message, Some(false));
            let text = self.result.0.clone();
            self.alert("Program Tag", &text);
            return;
        }
        let image = if s.retest && !matches!(s.flash, FlashCheck::Matches | FlashCheck::Differs(_))
        {
            None
        } else {
            s.image.clone()
        };
        let file_name = |i: &FirmwareImage| {
            Path::new(&i.elf_path)
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default()
        };
        let row = ProgramRecord {
            label: s.label.clone(),
            tag_type: image
                .as_ref()
                .map(|i| i.identity.target.clone())
                .filter(|t| !t.is_empty())
                .unwrap_or_else(|| self.target.clone()),
            uuid: if s.uuid.is_empty() {
                prior_uuid
            } else {
                s.uuid.clone()
            },
            firmware_version: image
                .as_ref()
                .map(|i| i.version().to_owned())
                .unwrap_or_default(),
            git_hash: if s.git_hash.is_empty() {
                image
                    .as_ref()
                    .map(|i| i.identity.git_hash.clone())
                    .unwrap_or_default()
            } else {
                s.git_hash.clone()
            },
            date: Utc::now(),
            test_status: s.test_status.clone(),
            result: s.result.clone(),
            image_sha256: image.as_ref().map(|i| i.sha256.clone()).unwrap_or_default(),
            image: image.as_ref().map(file_name).unwrap_or_default(),
            erase: if s.retest {
                String::new()
            } else {
                image
                    .as_ref()
                    .map(|i| i.plan.record_label().to_owned())
                    .unwrap_or_default()
            },
            voltage: s.voltage.map_or(-1.0, f64::from),
            probe_serial: s.serial.clone(),
            host_version: host_version(),
        };
        let written = match self.record.as_mut().map(|r| r.append(&row)) {
            Some(Ok(())) => true,
            Some(Err(e)) => {
                error!(
                    "The record was NOT written: {e} Row: {}",
                    row.fields().join(",")
                );
                message =
                    format!("{message}\n\nThe record was NOT written: {e} The row is in the log.");
                false
            }
            None => {
                let e = "No record file is open.";
                error!(
                    "The record was NOT written: {e} Row: {}",
                    row.fields().join(",")
                );
                message =
                    format!("{message}\n\nThe record was NOT written: {e} The row is in the log.");
                false
            }
        };
        let ok = written && row.result == "OK";
        if written {
            if ok {
                self.ok_count += 1
            } else {
                self.failed_count += 1
            }
        }
        self.check_record();
        self.result = (
            if row.result.is_empty() {
                message.clone()
            } else {
                row.result.clone()
            },
            Some(ok),
        );
        if let Some(s) = self.session.as_mut() {
            s.step_text = message.clone();
        }
        if !ok {
            self.raise(json!({ "kind": "alert", "title": "Program Tag", "text": message, "style": "warning" }));
        }
    }
}
