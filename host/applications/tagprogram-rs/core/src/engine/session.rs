//! The programming session: qtprogram's `ProgramSession`, ported.
//!
//! A session stops and erases the tag through its monitor, programs it with
//! probe-rs on a worker thread, reconnects, runs the self-tests, and gives
//! the tag one start, stop and erase so it leaves IDLE and empty. A retest
//! reads the flash back and compares it with the image instead of
//! programming. Each step is driven by a 1 s tick, as qtprogram's was; the
//! row is written to the record when the session ends.

use std::path::Path;
use std::time::{Duration, Instant};

use chrono::Utc;
use log::{error, info, warn};
use serde_json::json;
use tagcore::proto::{TagState, TagType, TestReq, TestResult};
use tagprobe::Progress;

use super::Msg;
use super::station::{Check, Station, state_name, test_name};
use crate::model::flashplan::Erase;
use crate::model::image::{FirmwareImage, compare_image_flash, image_flash_range, same_commit};
use crate::model::record::ProgramRecord;
use crate::model::settings as key;

const STOP_TICKS: u32 = 30;
const CONNECT_TICKS: u32 = 6;
const TEST_TICKS: u32 = 120;
const IMU_LOG_PAGE: i64 = 2048;
const SECTOR: i64 = 4096;
const TICK: Duration = Duration::from_secs(1);
/// What the record's `host_version` column says wrote the row: the host
/// tools package version when CMake builds the app (qtprogram records the
/// same number), else the crate's.
fn host_version() -> String {
    let version = option_env!("TAGPROGRAM_HOST_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"));
    format!("tagprogram-rs {version}")
}

/// Bytes as whole external-flash sectors, rounded up.
fn sectors(bytes: i64) -> i64 {
    (bytes.max(0) + SECTOR - 1) / SECTOR
}

/// The session's steps (`ProgramSession::Step`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Step {
    Stopping,
    Erasing,
    Programming,
    Reading,
    Connecting,
    PowerCycle,
    Testing,
    Done,
}

/// What a retest learned about the flash.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum FlashCheck {
    NotChecked,
    Unread(String),
    Matches,
    Differs(String),
}

/// One session's state.
pub(crate) struct SessionState {
    pub running: bool,
    pub step: Step,
    pub step_text: String,
    /// `None`: the busy indicator; `Some((done, total))` otherwise.
    pub progress: Option<(u64, u64)>,
    /// The next tick, while the timer runs.
    pub tick_at: Option<Instant>,
    ticks: u32,
    retest: bool,
    /// In the final start, stop and erase pass.
    pub finishing: bool,
    programmed: bool,
    commit_mismatch: bool,
    label: String,
    image: Option<FirmwareImage>,
    voltage: Option<f32>,
    serial: String,
    uuid: String,
    git_hash: String,
    test_status: String,
    flash: FlashCheck,
    pub result: String,
    pub message: String,
}

impl SessionState {
    fn new(label: &str, image: Option<FirmwareImage>, retest: bool) -> Self {
        Self {
            running: true,
            step: Step::Stopping,
            step_text: String::new(),
            progress: None,
            tick_at: None,
            ticks: 0,
            retest,
            finishing: false,
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
    /// programming. qtprogram also offered it in the final erase pass and
    /// then recorded the programmed tag as CANCELLED; this port does not.
    pub(crate) fn cancellable(&self) -> bool {
        self.running
            && (self.step == Step::PowerCycle
                || (!self.finishing && matches!(self.step, Step::Stopping | Step::Erasing)))
    }
}

impl Station {
    fn sess(&mut self) -> &mut SessionState {
        self.session.as_mut().expect("a session")
    }

    fn set_step(&mut self, step: Step, text: impl Into<String>) {
        let text = text.into();
        info!("{text}");
        let s = self.sess();
        s.step = step;
        s.step_text = text;
        s.progress = None;
    }

    fn start_timer(&mut self) {
        let s = self.sess();
        s.ticks = 0;
        s.tick_at = Some(Instant::now() + TICK);
    }

    fn stop_timer(&mut self) {
        self.sess().tick_at = None;
    }

    // ---- starting ----------------------------------------------------

    /// qtprogram's `Station::program`: after the Program dialog.
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
    pub(crate) fn retest(&mut self, label: &str) {
        self.check_record();
        if self.session_running() || self.level(Check::Record) != super::station::Level::Ok {
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
    /// else the newest cached release with the tag's target and commit.
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

    fn tick_stopping(&mut self) {
        let Ok(status) = self.tag.status() else {
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
            TagState::Idle if self.sess().finishing => {
                let _ = self.tag.set_rtc();
                let s = self.sess();
                let (result, message) = if s.commit_mismatch {
                    (
                        "MISMATCH".to_owned(),
                        format!(
                            "Programmed, but the tag reports commit {}, which is not the image's. It is not running the image just written.",
                            s.git_hash
                        ),
                    )
                } else if s.test_status == TestResult::AllPassed.as_str_name() {
                    (
                        "OK".to_owned(),
                        "Programmed and tested: ALL_PASSED. The tag is erased and IDLE.".to_owned(),
                    )
                } else {
                    (
                        "TEST_FAILED".to_owned(),
                        format!("Programmed, but the self-test reported {}.", s.test_status),
                    )
                };
                self.finish(&result, &message);
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

    fn tick_erasing(&mut self) {
        let status = self.tag.status().ok();
        let state = status
            .as_ref()
            .and_then(|s| TagState::try_from(s.state).ok());
        match (state, &status) {
            (Some(TagState::SReset), Some(st)) => {
                let total = if st.erase_sectors_total_plus_one > 0 {
                    i64::from(st.erase_sectors_total_plus_one - 1)
                } else {
                    self.info.as_ref().map_or(0, |info| {
                        if info.tag_type == TagType::Imutag as i32 {
                            sectors(i64::from(st.external_data_count) * IMU_LOG_PAGE)
                        } else {
                            sectors(info.extflashsz)
                        }
                    })
                };
                self.sess().progress = Some((st.sectors_erased.max(0) as u64, total.max(0) as u64));
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

    /// Attached with the firmware answering, attaching if need be.
    fn reattach_tag(&mut self) -> bool {
        if self.attached() && self.tag.tag_info().is_ok() {
            return true;
        }
        self.reattach() && self.tag.tag_info().is_ok()
    }

    fn tick_testing(&mut self) {
        let Ok(status) = self.tag.status() else {
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
        let _ = self.tag.set_rtc();
        let started = self.tag.config().and_then(|cfg| self.tag.start(&cfg));
        if started.is_err() {
            self.finish(
                "FAILED: no start after testing",
                "The programmed tag would not start for its final erase.",
            );
            return;
        }
        self.sess().finishing = true;
        info!("Starting, stopping and erasing the tag once");
        let s = self.sess();
        s.step_text = "Starting, stopping and erasing the tag once".into();
        s.step = Step::Stopping;
        s.progress = None;
        s.ticks = 0;
    }

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
        info!(
            "probe-rs: {chip}, {}, erase {erase:?}",
            driver.name()
        );
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

    pub(crate) fn session_retry_connect(&mut self) {
        if self
            .session
            .as_ref()
            .is_some_and(|s| s.running && s.step == Step::PowerCycle)
        {
            self.set_step(Step::Connecting, "Connecting to the new image");
            self.start_timer();
        }
    }

    pub(crate) fn session_cancel(&mut self) {
        let Some(s) = self.session.as_ref().filter(|s| s.cancellable()) else {
            return;
        };
        if s.step == Step::PowerCycle {
            self.finish(
                "NO_ANSWER",
                "Programmed, but the tag never answered. Power-cycle it and press Attach; if it still does not answer, program it again.",
            );
        } else {
            self.finish("CANCELLED", "Cancelled before programming.");
        }
    }

    // ---- the end -----------------------------------------------------

    fn finish(&mut self, result: &str, message: &str) {
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
                    "The record was NOT written: {e}. Row: {}",
                    row.fields().join(",")
                );
                message =
                    format!("{message}\n\nThe record was NOT written: {e}. The row is in the log.");
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
