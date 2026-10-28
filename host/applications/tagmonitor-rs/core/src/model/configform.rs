//! The Configuration tab's form: the panels that edit a [`Config`], with no
//! UI toolkit.
//!
//! Port of `host/applications/qtmon/tagconfiguration/` (the panels and
//! `ConfigTab`'s panel table). Each panel owns a fixed set of Config fields,
//! [`load`](ConfigForm::load)s them into its controls and
//! [`collect`](ConfigForm::collect)s them back; fields no panel owns pass
//! through. What a panel sends depends on how Qt's widgets behave, not only
//! on the panel's own code, so the widgets are modelled here too:
//!
//! - a [`DateEdit`] clamps to its range, and every change runs its owner's
//!   handler again (the schedule's whole-hour rounding, the hibernation
//!   rows' start-before-end rule), as `QDateTimeEdit::dateTimeChanged` does;
//! - a [`DoubleSpin`] rounds to its decimals (halves away from zero, as Qt's
//!   `QString::number(v, 'f', n)`), then clamps, and re-rounds its limits
//!   when its decimals change, as `QDoubleSpinBox` does;
//! - an [`IntSpin`] clamps; a [`RadioGroup`] keeps its choice when asked to
//!   check an id it has no button for.
//!
//! The probe goldens in `host/applications/qtmon/test/golden/` check all of
//! it: `tests/config_probe.rs` replays every case and compares what Start
//! sends, byte for byte, and [`ConfigForm::fingerprint`] with the golden's
//! description of every control.

use serde_json::{Value, json};
use tagcore::proto::{
    Adxl362, BitTagLogFmt, Config, Lsm6dsv, TagType, adxl362, config::Interval, lsm6dsv,
};
use tagcore::textformat::format_g;

/// QDateTimeEdit's default earliest value: 14 September 1752, UTC.
pub const DATE_MIN: i64 = -6_857_222_400;
/// QDateTimeEdit's default latest value: 31 December 9999 23:59:59, UTC.
pub const DATE_MAX: i64 = 253_402_300_799;
/// The value a QDateTimeEdit starts with: 1 January 2000. Only whether a
/// later value differs from it matters (a change runs the handlers).
const DATE_INITIAL: i64 = 946_684_800;
/// The display format of every schedule time; minutes are always ":00".
pub const DATE_FORMAT: &str = "M/d/yy h:00 AP";
/// An interval end that means "no end".
const FOREVER: i32 = i32::MAX;

// ---- Widgets ---------------------------------------------------------------

/// `epoch` rounded up to a multiple of `seconds`, with C's truncating `%`, as
/// `roundEpoch()` in `schedulepanel.cpp`.
fn round_epoch(epoch: i64, seconds: i64) -> i64 {
    let e = epoch + seconds - 1;
    e - e % seconds
}

/// Rounds up to the next whole hour (HourTime::ceilToHour).
fn ceil_hour(epoch: i64) -> i64 {
    let over = epoch.rem_euclid(3600);
    if over == 0 {
        epoch
    } else {
        epoch + 3600 - over
    }
}

/// Rounds down to a whole hour (HourTime::floorToHour).
fn floor_hour(epoch: i64) -> i64 {
    epoch - epoch.rem_euclid(3600)
}

/// A date and time box (QDateTimeEdit), in seconds since the epoch.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DateEdit {
    /// The value.
    pub value: i64,
    /// The earliest value.
    pub min: i64,
    /// The latest value.
    pub max: i64,
}

impl DateEdit {
    /// A box with Qt's default range and starting value.
    fn new() -> Self {
        DateEdit {
            value: DATE_INITIAL,
            min: DATE_MIN,
            max: DATE_MAX,
        }
    }
    /// Sets the value, clamped; true when it changed (Qt then emits).
    fn assign(&mut self, v: i64) -> bool {
        let v = v.clamp(self.min, self.max);
        let changed = v != self.value;
        self.value = v;
        changed
    }
    /// Sets the earliest value, raising the latest and the value to it if
    /// they are below; true when the value changed.
    fn assign_min(&mut self, min: i64) -> bool {
        self.min = min;
        self.max = self.max.max(min);
        let v = self.value;
        self.assign(v)
    }
}

/// C's rounding to `decimals` places, as Qt's `QString::number(v, 'f', n)`
/// does it: on the exact binary value, halves away from zero.
pub fn qt_round(v: f64, decimals: u32) -> f64 {
    if !v.is_finite() {
        return v;
    }
    // Every double's decimal expansion ends within 1074 places.
    let exact = format!("{:.1100}", v.abs());
    let (int, frac) = exact.split_once('.').expect("fixed notation");
    let d = decimals as usize;
    let mut digits: Vec<u8> = format!("{int}{}", &frac[..d]).into_bytes();
    if frac.as_bytes()[d] >= b'5' {
        let mut i = digits.len();
        loop {
            if i == 0 {
                digits.insert(0, b'1');
                break;
            }
            i -= 1;
            if digits[i] == b'9' {
                digits[i] = b'0';
            } else {
                digits[i] += 1;
                break;
            }
        }
    }
    let text = String::from_utf8(digits).expect("digits");
    let split = text.len() - d;
    let rounded: f64 = format!("{}.{}0", &text[..split], &text[split..])
        .parse()
        .expect("number");
    if v.is_sign_negative() {
        -rounded
    } else {
        rounded
    }
}

/// A decimal spin box (QDoubleSpinBox).
#[derive(Debug, Clone, PartialEq)]
pub struct DoubleSpin {
    /// The value, rounded to `decimals`.
    pub value: f64,
    /// The lowest value, rounded.
    pub min: f64,
    /// The highest value, rounded; never below `min`.
    pub max: f64,
    /// The range as last asked for, unrounded, for re-rounding.
    actual: (f64, f64),
    /// The step of the arrows.
    pub step: f64,
    /// Decimal places shown and kept.
    pub decimals: u32,
}

impl DoubleSpin {
    /// Qt's defaults: 0 to 99.99, step 1, two decimals, value 0.
    fn new() -> Self {
        DoubleSpin {
            value: 0.0,
            min: 0.0,
            max: 99.99,
            actual: (0.0, 99.99),
            step: 1.0,
            decimals: 2,
        }
    }
    /// The value clamped to the range.
    fn bound(&self, v: f64) -> f64 {
        if v < self.min {
            self.min
        } else if v > self.max {
            self.max
        } else {
            v
        }
    }
    /// setRange: the limits rounded; the value clamped into them.
    fn set_range(&mut self, min: f64, max: f64) {
        self.actual = (min, max);
        self.min = qt_round(min, self.decimals);
        let max = qt_round(max, self.decimals);
        self.max = if self.min < max { max } else { self.min };
        self.value = self.bound(self.value);
    }
    /// setDecimals: re-rounds the range and the value.
    fn set_decimals(&mut self, decimals: u32) {
        self.decimals = decimals;
        let (min, max) = self.actual;
        self.set_range(min, max);
        self.set_value(self.value);
    }
    /// setValue: rounded, then clamped.
    pub fn set_value(&mut self, v: f64) {
        self.value = self.bound(qt_round(v, self.decimals));
    }
}

/// An integer spin box (QSpinBox).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IntSpin {
    /// The value.
    pub value: i32,
    /// The lowest value.
    pub min: i32,
    /// The highest value.
    pub max: i32,
    /// The step of the arrows.
    pub step: i32,
}

impl IntSpin {
    /// setValue: clamped.
    pub fn set_value(&mut self, v: i32) {
        self.value = v.clamp(self.min, self.max);
    }
}

/// One radio button: its enum value, text and tooltip.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Choice {
    /// The enum value it stands for (PBEnumGroup's button id).
    pub id: i32,
    /// Its text.
    pub text: &'static str,
    /// Its own tooltip, or empty.
    pub tooltip: &'static str,
}

/// Shorthand for a [`Choice`].
const fn choice(id: i32, text: &'static str, tooltip: &'static str) -> Choice {
    Choice { id, text, tooltip }
}

/// An exclusive group of radio buttons (PBEnumGroup), the first checked at
/// first.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RadioGroup {
    /// The buttons, in order.
    pub choices: &'static [Choice],
    /// The checked button's id.
    pub checked: i32,
}

impl RadioGroup {
    /// The group with its first button checked.
    fn new(choices: &'static [Choice]) -> Self {
        RadioGroup {
            choices,
            checked: choices[0].id,
        }
    }
    /// Checks the button for `id`; an id with no button changes nothing.
    pub fn set_checked(&mut self, id: i32) {
        if self.choices.iter().any(|c| c.id == id) {
            self.checked = id;
        }
    }
}

// ---- Controls, as the UI and the probe see them ------------------------

/// What a control is and holds.
#[derive(Debug, Clone, PartialEq)]
pub enum ControlKind {
    /// Radio buttons; `checked` is the chosen [`Choice::id`].
    Radio {
        /// The buttons.
        choices: &'static [Choice],
        /// The checked one.
        checked: i32,
    },
    /// A check box.
    Check {
        /// Its text.
        text: &'static str,
        /// Ticked.
        checked: bool,
    },
    /// A decimal spin box.
    Double(DoubleSpin, &'static str),
    /// An integer spin box.
    Int(IntSpin),
    /// A whole-hour UTC date and time.
    DateTime(DateEdit),
}

/// One control of a shown panel.
#[derive(Debug, Clone, PartialEq)]
pub struct Control {
    /// What an edit names it by (`"start"`, `"adxl.range"`, ...).
    pub id: String,
    /// The titles of the groups around it, outermost first.
    pub path: &'static [&'static str],
    /// Its form label, or empty.
    pub label: &'static str,
    /// Its tooltip (a radio group's own), or empty.
    pub tooltip: &'static str,
    /// It can be edited now.
    pub enabled: bool,
    /// What it holds.
    pub kind: ControlKind,
}

/// `%.9g`, the probe's number format.
fn num(v: f64) -> String {
    format_g(v, 9, 9, |_| true)
}

impl Control {
    /// The probe's description lines (`configprobe.cpp`, `describe()`):
    /// one per radio button, one for anything else.
    pub fn fingerprint(&self) -> Vec<String> {
        let path = self.path.join(" > ");
        let tail = |enabled: bool, tooltip: &str| {
            let mut t = format!("enabled={}", u8::from(enabled));
            if !tooltip.is_empty() {
                t += &format!(" tooltip=\"{tooltip}\"");
            }
            t
        };
        let line = |body: String| format!("{path} | {body}");
        match &self.kind {
            ControlKind::Radio { choices, checked } => choices
                .iter()
                .map(|c| {
                    line(format!(
                        "radio text=\"{}\" checked={} {}",
                        c.text,
                        u8::from(c.id == *checked),
                        tail(self.enabled, c.tooltip)
                    ))
                })
                .collect(),
            ControlKind::Check { text, checked } => vec![line(format!(
                "check text=\"{text}\" checked={} {}",
                u8::from(*checked),
                tail(self.enabled, self.tooltip)
            ))],
            ControlKind::Double(d, suffix) => vec![line(format!(
                "double label=\"{}\" value={} min={} max={} step={} decimals={} prefix=\"\" suffix=\"{suffix}\" {}",
                self.label,
                num(d.value),
                num(d.min),
                num(d.max),
                num(d.step),
                d.decimals,
                tail(self.enabled, self.tooltip)
            ))],
            ControlKind::Int(i) => vec![line(format!(
                "int label=\"{}\" value={} min={} max={} step={} prefix=\"\" suffix=\"\" {}",
                self.label,
                i.value,
                i.min,
                i.max,
                i.step,
                tail(self.enabled, self.tooltip)
            ))],
            ControlKind::DateTime(d) => vec![line(format!(
                "datetime label=\"{}\" value={} min={} max={} format=\"{DATE_FORMAT}\" {}",
                self.label,
                d.value,
                d.min,
                d.max,
                tail(self.enabled, self.tooltip)
            ))],
        }
    }

    /// The control for the page: its id, kind, label, tooltip, enabled and
    /// value, with its range and choices.
    pub fn view(&self) -> Value {
        let mut v = json!({
            "id": self.id, "group": self.path.last().copied().unwrap_or(""),
            "label": self.label, "tooltip": self.tooltip, "enabled": self.enabled,
        });
        let extra = match &self.kind {
            ControlKind::Radio { choices, checked } => json!({
                "kind": "radio", "value": checked,
                "choices": choices.iter().map(|c| json!({ "id": c.id, "text": c.text.trim_end(), "tooltip": c.tooltip })).collect::<Vec<_>>(),
            }),
            ControlKind::Check { text, checked } => {
                json!({ "kind": "check", "text": text, "value": checked })
            }
            ControlKind::Double(d, suffix) => json!({
                "kind": "number", "value": d.value, "min": d.min, "max": d.max, "step": d.step,
                "decimals": d.decimals, "suffix": suffix.trim(),
            }),
            ControlKind::Int(i) => json!({
                "kind": "number", "value": i.value, "min": i.min, "max": i.max, "step": i.step, "decimals": 0, "suffix": "",
            }),
            ControlKind::DateTime(d) => {
                json!({ "kind": "datetime", "value": d.value, "min": d.min, "max": d.max })
            }
        };
        for (k, x) in extra.as_object().expect("object") {
            v[k] = x.clone();
        }
        v
    }
}

// ---- The schedule panel ------------------------------------------------------

/// Which fields the schedule panel owns (SchedulePanel::Variant).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ScheduleVariant {
    /// active_interval and hibernate.
    Interval,
    /// The above and PresTag's period.
    IntervalWithPeriod,
    /// IMUTag's start_delay only.
    StartDelay,
}

/// Rows the hibernation group has (the most any firmware accepts).
const HIBERNATE_ROWS: usize = 2;

/// One hibernation row (Hibernate): In use, start and end.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HibernateRow {
    /// In use; the times are editable only while it is.
    pub in_use: bool,
    /// Start of hibernation, rounded down to the hour.
    pub start: DateEdit,
    /// End of hibernation, rounded up to the hour.
    pub end: DateEdit,
}

impl HibernateRow {
    /// A new, unused row.
    fn new() -> Self {
        HibernateRow {
            in_use: false,
            start: DateEdit::new(),
            end: DateEdit::new(),
        }
    }
    /// The start box's setDateTime and its handler.
    fn set_start(&mut self, v: i64) {
        if !self.start.assign(v) {
            return;
        }
        let dt = self.start.value;
        if floor_hour(dt) != dt {
            return self.set_start(floor_hour(dt));
        }
        if dt > self.end.value {
            self.set_end(dt);
        }
    }
    /// The end box's setDateTime and its handler.
    fn set_end(&mut self, v: i64) {
        if !self.end.assign(v) {
            return;
        }
        let dt = self.end.value;
        if ceil_hour(dt) != dt {
            return self.set_end(ceil_hour(dt));
        }
        if dt < self.start.value {
            self.set_start(dt);
        }
    }
}

/// The schedule panel (SchedulePanel): start, run, hibernation, and the
/// period or start delay.
#[derive(Debug, Clone, PartialEq)]
pub struct Schedule {
    /// Its fields.
    variant: ScheduleVariant,
    /// Start Immediately (else On the start time).
    pub start_now: bool,
    /// Run Indefinitely (else Until the end time).
    pub run_indefinitely: bool,
    /// The start time.
    pub start: DateEdit,
    /// The end time.
    pub end: DateEdit,
    /// The hibernation rows; the first `row_count` are shown.
    pub rows: [HibernateRow; HIBERNATE_ROWS],
    /// Rows the last load showed.
    pub row_count: usize,
    /// PresTag's sample period, seconds.
    pub period: IntSpin,
    /// IMUTag's start delay, minutes.
    pub start_delay: IntSpin,
}

impl Schedule {
    /// The panel, its groups chosen by `variant`.
    fn new(variant: ScheduleVariant) -> Self {
        Schedule {
            variant,
            start_now: true,
            run_indefinitely: true,
            start: DateEdit::new(),
            end: DateEdit::new(),
            rows: [HibernateRow::new(), HibernateRow::new()],
            row_count: 0,
            period: IntSpin {
                value: 10,
                min: 1,
                max: 300,
                step: 10,
            },
            start_delay: IntSpin {
                value: 0,
                min: 0,
                max: 120,
                step: 1,
            },
        }
    }
    /// It owns the interval and hibernation.
    fn interval(&self) -> bool {
        self.variant != ScheduleVariant::StartDelay
    }

    /// The start box's setDateTime and its handler: whole hours, and the
    /// end may not be earlier.
    fn set_start(&mut self, v: i64) {
        if self.start.assign(v) {
            self.start_changed();
        }
    }
    /// on_StartDateTime_dateTimeChanged.
    fn start_changed(&mut self) {
        let dt = self.start.value;
        if ceil_hour(dt) != dt {
            return self.set_start(ceil_hour(dt));
        }
        if self.end.assign_min(dt) {
            self.end_changed();
        }
    }
    /// The end box's setDateTime and its handler.
    fn set_end(&mut self, v: i64) {
        if self.end.assign(v) {
            self.end_changed();
        }
    }
    /// on_EndDateTime_dateTimeChanged: whole hours.
    fn end_changed(&mut self) {
        let dt = self.end.value;
        if ceil_hour(dt) != dt {
            self.set_end(ceil_hour(dt));
        }
    }

    /// SchedulePanel::load; `now` is the clock, seconds.
    fn load(&mut self, config: &Config, now: i64) {
        let now = round_epoch(now, 60);
        if self.interval() {
            let (start_epoch, end_epoch) = match &config.active_interval {
                Some(i) => (i.start_epoch, i.end_epoch),
                None => (0, FOREVER),
            };
            let start = round_epoch(i64::from(start_epoch), 60);
            self.run_indefinitely = end_epoch == FOREVER;
            self.set_end(i64::from(end_epoch));
            if self.start.assign_min(now) {
                self.start_changed();
            }
            if start < now {
                self.start_now = true;
                self.set_start(now);
            } else {
                self.start_now = false;
                self.set_start(start);
            }
            if self.run_indefinitely {
                self.set_end(self.start.value);
            }
            // An unset row (0 or INT32_MAX) loads as an unused row at 0 to 0.
            self.row_count = config.hibernate.len().min(HIBERNATE_ROWS);
            for (i, row) in self.rows.iter_mut().enumerate().take(self.row_count) {
                let h = &config.hibernate[i];
                if h.start_epoch == FOREVER || h.start_epoch == 0 {
                    row.set_start(0);
                    row.set_end(0);
                    row.in_use = false;
                } else {
                    row.set_start(i64::from(h.start_epoch));
                    row.set_end(i64::from(h.end_epoch));
                    row.in_use = true;
                }
            }
        }
        // A zero period leaves the previous value: the firmware rejects 0.
        if self.variant == ScheduleVariant::IntervalWithPeriod && config.period != 0 {
            self.period.set_value(config.period as i32);
        }
        if self.variant == ScheduleVariant::StartDelay {
            let minutes = if config.start_delay > 0 {
                config.start_delay - 1
            } else {
                0
            };
            self.start_delay.set_value(minutes as i32);
        }
    }

    /// SchedulePanel::store; `now` is the clock, seconds.
    fn store(&self, config: &mut Config, now: i64) {
        if self.interval() {
            let start = self.start.value;
            let end = self.end.value;
            let start_epoch = if self.start_now || start < now {
                0
            } else {
                start as i32
            };
            let end_epoch = if !self.run_indefinitely && end < i64::from(FOREVER) {
                end as i32
            } else {
                FOREVER
            };
            config.active_interval = Some(Interval {
                start_epoch,
                end_epoch,
            });
            config.hibernate = self.rows[..self.row_count]
                .iter()
                .map(|r| Interval {
                    start_epoch: if r.in_use { r.start.value as i32 } else { 0 },
                    end_epoch: if r.in_use { r.end.value as i32 } else { 0 },
                })
                .collect();
        }
        if self.variant == ScheduleVariant::IntervalWithPeriod {
            config.period = self.period.value as u32;
        }
        if self.variant == ScheduleVariant::StartDelay {
            config.start_delay = (self.start_delay.value + 1) as u32;
        }
    }

    /// The user ticked or cleared a row's In use (hibernateInUseClicked): a
    /// row first put in use starts at the logging start, rounded up to the
    /// hour, and lasts an hour.
    fn in_use_clicked(&mut self, row: usize, on: bool) {
        let start = round_epoch(self.start.value, 3600);
        let r = &mut self.rows[row];
        r.in_use = on;
        if !on || r.start.value as i32 > 0 {
            return;
        }
        r.set_start(i64::from(start as i32));
        r.set_end(i64::from((start + 3600) as i32));
    }

    /// The panel's shown controls.
    fn controls(&self, enabled: bool) -> Vec<Control> {
        const START: &[&str] = &["Logging Schedule", "Start"];
        const RUN: &[&str] = &["Logging Schedule", "Run"];
        const HIB: &[&str] = &["Logging Schedule", "Hibernation"];
        static START_MODE: [Choice; 2] = [
            choice(
                0,
                "Immediately",
                "Start data collection as soon as possible.",
            ),
            choice(1, "On   ", "Start data collection at a specific time."),
        ];
        static RUN_MODE: [Choice; 2] = [
            choice(
                0,
                "Indefinitely",
                "Collect data for as long as possible, until either battery or storage runs out.",
            ),
            choice(1, "Until", "Stop collecting data at a specific time."),
        ];
        let c = |id: &str, path, label, tooltip, en, kind| Control {
            id: id.into(),
            path,
            label,
            tooltip,
            enabled: en,
            kind,
        };
        let mut out = Vec::new();
        if self.interval() {
            out.push(c(
                "start_mode",
                START,
                "",
                "Set data collection starting conditions.",
                enabled,
                ControlKind::Radio {
                    choices: &START_MODE,
                    checked: i32::from(!self.start_now),
                },
            ));
            out.push(c(
                "start",
                START,
                "",
                "Programmed date/time to start data collection (UTC, whole hours)",
                enabled,
                ControlKind::DateTime(self.start.clone()),
            ));
            out.push(c(
                "run_mode",
                RUN,
                "",
                "Set data collection ending conditions.",
                enabled,
                ControlKind::Radio {
                    choices: &RUN_MODE,
                    checked: i32::from(!self.run_indefinitely),
                },
            ));
            out.push(c(
                "end",
                RUN,
                "",
                "Programmed date/time to halt data collection (UTC, whole hours)",
                enabled,
                ControlKind::DateTime(self.end.clone()),
            ));
            for (i, r) in self.rows.iter().enumerate().take(self.row_count) {
                out.push(c(
                    &format!("hib{i}.in_use"),
                    HIB,
                    "",
                    "Hibernate between the start and end below. Unchecked, the row is not used.",
                    enabled,
                    ControlKind::Check {
                        text: "In use",
                        checked: r.in_use,
                    },
                ));
                out.push(c(
                    &format!("hib{i}.start"),
                    HIB,
                    "Start",
                    "Start hibernating at this date/time (UTC, whole hours)",
                    enabled && r.in_use,
                    ControlKind::DateTime(r.start.clone()),
                ));
                out.push(c(&format!("hib{i}.end"), HIB, "End",
                    "Stop hibernating at this date/time (UTC, whole hours). The tag checks once an hour, on the hour, whether to stop hibernating.",
                    enabled && r.in_use, ControlKind::DateTime(r.end.clone())));
            }
        }
        if self.variant == ScheduleVariant::IntervalWithPeriod {
            out.push(c(
                "period",
                &["Logging Schedule", "Period"],
                "",
                "Data sample period",
                enabled,
                ControlKind::Int(self.period.clone()),
            ));
        }
        if self.variant == ScheduleVariant::StartDelay {
            out.push(c(
                "start_delay",
                &["Logging Schedule", "Start Delay"],
                "",
                "Start delay in minutes -- rounded to next minute",
                enabled,
                ControlKind::Int(self.start_delay.clone()),
            ));
        }
        out
    }

    /// One user edit; false when `id` is not this panel's.
    fn edit(&mut self, id: &str, v: &Value) -> bool {
        let int = v.as_i64().unwrap_or(0);
        match id {
            // Choosing Immediately or On changes nothing else: the groups'
            // click handlers never run, since the groups are not checkable.
            "start_mode" => self.start_now = int == 0,
            "run_mode" => self.run_indefinitely = int == 0,
            "start" => self.set_start(int),
            "end" => self.set_end(int),
            "period" => self.period.set_value(int as i32),
            "start_delay" => self.start_delay.set_value(int as i32),
            _ => {
                let Some((row, field)) = id.strip_prefix("hib").and_then(|r| r.split_once('.'))
                else {
                    return false;
                };
                let Some(row) = row.parse::<usize>().ok().filter(|r| *r < self.row_count) else {
                    return false;
                };
                match field {
                    "in_use" => self.in_use_clicked(row, v.as_bool().unwrap_or(false)),
                    "start" => self.rows[row].set_start(int),
                    "end" => self.rows[row].set_end(int),
                    _ => return false,
                }
            }
        }
        true
    }
}

// ---- The BitTag log panel ----------------------------------------------------

/// The BitTag log format buttons.
static BITTAG_LOG: [Choice; 4] = [
    choice(
        BitTagLogFmt::BittagBitpersec as i32,
        "Activity Bit Per Second",
        "",
    ),
    choice(
        BitTagLogFmt::BittagBitspermin as i32,
        "Activity Count Per Minute",
        "",
    ),
    choice(
        BitTagLogFmt::BittagBitsperfourmin as i32,
        "Activity Count Per Four Minutes",
        "",
    ),
    choice(
        BitTagLogFmt::BittagBitsperfivemin as i32,
        "Activity Count Per Five Minutes",
        "",
    ),
];

/// The message BitTagLogPanel shows for a BitTag with no log format.
pub const MISSING_BITTAG_LOG: &str = "Configuring BitTag but missing log specification";

// ---- The accelerometer panel -------------------------------------------------

/// Which meaning of the adxl362 fields the panel edits (Adxl362Panel::Variant).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AdxlVariant {
    /// The classic BitTag: range, rate, filter and three thresholds.
    Standard,
    /// BitTag LE and BitPresTag wake mode.
    Wake362,
    /// BitTagNG and UIUCTag ADXL367 wake-up.
    Wake367,
}

/// Range buttons (Standard).
static ADXL_RANGE: [Choice; 3] = [
    choice(adxl362::Rng::R2g as i32, "+/- 2g", ""),
    choice(adxl362::Rng::R4g as i32, "+/- 4g", ""),
    choice(adxl362::Rng::R8g as i32, "+/- 8g", ""),
];
/// Sample rate buttons (Standard).
static ADXL_ODR: [Choice; 6] = [
    choice(adxl362::Odr::S125 as i32, "12.5 Hz", "12.5 samples/second"),
    choice(adxl362::Odr::S25 as i32, "25 Hz", "25 samples/second"),
    choice(adxl362::Odr::S50 as i32, "50 Hz", "50 samples/second"),
    choice(adxl362::Odr::S100 as i32, "100 Hz", "100 samples/second"),
    choice(adxl362::Odr::S200 as i32, "200 Hz", "200 samples/second"),
    choice(adxl362::Odr::S400 as i32, "400 Hz", "400 samples/second"),
];
/// Anti-alias filter buttons (Standard).
static ADXL_FILTER: [Choice; 2] = [
    choice(
        adxl362::Aa::AAquarter as i32,
        "1/4 Sample Rate",
        "filter cutoff -- 1/4 sample rate",
    ),
    choice(
        adxl362::Aa::AAhalf as i32,
        "1/2 Sample Rate",
        "filter cutoff -- 1/2 sample rate",
    ),
];

/// The accelerometer panel (Adxl362Panel).
#[derive(Debug, Clone, PartialEq)]
pub struct Adxl {
    /// Which meaning the fields have.
    variant: AdxlVariant,
    /// Also writes accel_type (BitTagNG).
    owns_accel_type: bool,
    /// The last load saw accel_type ADXL367.
    reported_367: bool,
    /// Range, rate and filter (Standard only).
    pub range: RadioGroup,
    /// Sample rate (Standard only).
    pub freq: RadioGroup,
    /// Anti-alias filter (Standard only).
    pub filter: RadioGroup,
    /// act_thresh_g.
    pub act: DoubleSpin,
    /// inact_thresh_g (not Wake367).
    pub inact: DoubleSpin,
    /// inactive_sec: seconds or samples, by variant.
    pub inactive: DoubleSpin,
}

/// The threshold scale of a range (on_adxlrange_clicked): ±2, 4, 8 g gives
/// 1, 2, 4.
fn range_scale(id: i32) -> f64 {
    match adxl362::Rng::try_from(id) {
        Ok(adxl362::Rng::R4g) => 2.0,
        Ok(adxl362::Rng::R8g) => 4.0,
        _ => 1.0,
    }
}

/// The sample period of a rate (on_adxlfreq_clicked), seconds.
fn rate_period(id: i32) -> f64 {
    match adxl362::Odr::try_from(id) {
        Ok(adxl362::Odr::S25) => 1.0 / 25.0,
        Ok(adxl362::Odr::S50) => 1.0 / 50.0,
        Ok(adxl362::Odr::S100) => 1.0 / 100.0,
        Ok(adxl362::Odr::S200) => 1.0 / 200.0,
        Ok(adxl362::Odr::S400) => 1.0 / 400.0,
        _ => 1.0 / 12.5,
    }
}

impl Adxl {
    /// The panel, its limits fixed per variant in the constructor's order:
    /// QDoubleSpinBox rounds its limits to the decimals in force.
    fn new(variant: AdxlVariant, owns_accel_type: bool) -> Self {
        let mut a = Adxl {
            variant,
            owns_accel_type: variant == AdxlVariant::Wake367 && owns_accel_type,
            reported_367: false,
            range: RadioGroup::new(&ADXL_RANGE),
            freq: RadioGroup::new(&ADXL_ODR),
            filter: RadioGroup::new(&ADXL_FILTER),
            act: DoubleSpin::new(),
            inact: DoubleSpin::new(),
            inactive: DoubleSpin::new(),
        };
        match variant {
            AdxlVariant::Standard => {
                a.act.set_decimals(2);
                a.inactive.set_decimals(2);
            }
            AdxlVariant::Wake362 => {
                a.range_clicked(adxl362::Rng::R4g as i32);
                a.freq_clicked(adxl362::Odr::S125 as i32);
                a.act.set_decimals(2);
                a.inact.set_decimals(2);
                a.inactive.set_range(1.0, 255.0);
                a.inactive.step = 1.0;
                a.inactive.set_decimals(0);
            }
            AdxlVariant::Wake367 => {
                a.range_clicked(adxl362::Rng::R2g as i32);
                a.freq_clicked(adxl362::Odr::S125 as i32);
                a.act.set_range(0.2, 0.7);
                a.act.step = 0.05;
                a.act.set_decimals(2);
                a.inactive.set_range(3.0, 12.0);
                a.inactive.step = 1.0;
                a.inactive.set_decimals(0);
            }
        }
        a
    }
    /// The inactive threshold is a control.
    fn has_inact(&self) -> bool {
        self.variant != AdxlVariant::Wake367
    }
    /// on_adxlrange_clicked: thresholds over range/64 to range.
    fn range_clicked(&mut self, id: i32) {
        let r = range_scale(id);
        for spin in [&mut self.act, &mut self.inact] {
            spin.set_range(r / 64.0, r);
            spin.step = r / 64.0;
        }
    }
    /// on_adxlfreq_clicked: inactivity over 1 to 255 samples.
    fn freq_clicked(&mut self, id: i32) {
        let d = rate_period(id);
        self.inactive.set_range(d, d * 255.0);
        self.inactive.step = d;
    }

    /// Adxl362Panel::load.
    fn load(&mut self, config: &Config) {
        let adxl = config.adxl362.unwrap_or_default();
        self.reported_367 = adxl.accel_type == adxl362::AdxlType::AdxlType367 as i32;
        if self.variant == AdxlVariant::Standard {
            self.filter.set_checked(adxl.filter);
            self.freq.set_checked(adxl.freq);
            self.range.set_checked(adxl.range);
            self.freq_clicked(adxl.freq);
            self.range_clicked(adxl.range);
        }
        // Values after ranges, so out-of-range values clamp as displayed.
        if self.has_inact() {
            self.inact.set_value(f64::from(adxl.inact_thresh_g));
        }
        self.inactive.set_value(f64::from(adxl.inactive_sec));
        self.act.set_value(f64::from(adxl.act_thresh_g));
    }

    /// Adxl362Panel::store: onto the document's own adxl362.
    fn store(&self, config: &mut Config) {
        let mut adxl: Adxl362 = config.adxl362.unwrap_or_default();
        if self.owns_accel_type {
            adxl.accel_type = if self.reported_367 {
                adxl362::AdxlType::AdxlType367
            } else {
                adxl362::AdxlType::AdxlType362
            } as i32;
        }
        if self.variant == AdxlVariant::Standard {
            adxl.freq = self.freq.checked;
            adxl.range = self.range.checked;
            adxl.filter = self.filter.checked;
        }
        if self.has_inact() {
            adxl.inact_thresh_g = self.inact.value as f32;
        }
        adxl.inactive_sec = self.inactive.value as f32;
        adxl.act_thresh_g = self.act.value as f32;
        config.adxl362 = Some(adxl);
    }

    /// The panel's controls.
    fn controls(&self, enabled: bool) -> Vec<Control> {
        const STD: &[&str] = &["Accelerometer", "Activity Detection"];
        const WAKE: &[&str] = &["Activity Wakeup", "Activity Detection"];
        let c = |id: &str, path, label, tooltip, kind| Control {
            id: id.into(),
            path,
            label,
            tooltip,
            enabled,
            kind,
        };
        let dbl = |s: &DoubleSpin, suffix| ControlKind::Double(s.clone(), suffix);
        let radio = |g: &RadioGroup| ControlKind::Radio {
            choices: g.choices,
            checked: g.checked,
        };
        match self.variant {
            AdxlVariant::Standard => vec![
                c(
                    "adxl.range",
                    &["Accelerometer", "Range"],
                    "",
                    "Set Accelerometer Range",
                    radio(&self.range),
                ),
                c(
                    "adxl.freq",
                    &["Accelerometer", "Sample Rate"],
                    "",
                    "Set Accelerometer Sample Rate",
                    radio(&self.freq),
                ),
                c(
                    "adxl.filter",
                    &["Accelerometer", "Filter"],
                    "",
                    "Set Accelerometer Anti-Alias Filter",
                    radio(&self.filter),
                ),
                c(
                    "adxl.act",
                    STD,
                    "Active Threshold",
                    "Wakeup threshold",
                    dbl(&self.act, " g"),
                ),
                c(
                    "adxl.inact",
                    STD,
                    "Inactive Threshold",
                    "Sleep threshold",
                    dbl(&self.inact, " g"),
                ),
                c(
                    "adxl.inactive",
                    STD,
                    "Inactivity",
                    "Time below wakeup threshold to become inactive",
                    dbl(&self.inactive, " sec"),
                ),
            ],
            AdxlVariant::Wake362 => vec![
                c(
                    "adxl.act",
                    WAKE,
                    "Active Threshold",
                    "ADXL362 wake-up active threshold in g",
                    dbl(&self.act, " g"),
                ),
                c(
                    "adxl.inact",
                    WAKE,
                    "Inactive Threshold",
                    "ADXL362 wake-up inactive threshold in g",
                    dbl(&self.inact, " g"),
                ),
                c(
                    "adxl.inactive",
                    WAKE,
                    "Inactivity Samples",
                    "Samples below inactive threshold at 6 Hz",
                    dbl(&self.inactive, " samples"),
                ),
            ],
            AdxlVariant::Wake367 => vec![
                c(
                    "adxl.act",
                    WAKE,
                    "Wakeup Threshold",
                    "ADXL367 wake-up threshold in g",
                    dbl(&self.act, " g"),
                ),
                c(
                    "adxl.inactive",
                    WAKE,
                    "Inactivity Samples",
                    "Samples below the inactivity threshold before the ADXL367 declares inactivity. Counted at its 6.25 Hz wake rate, so one sample is ~160 ms (e.g. 3 samples ≈ 0.48 s, 12 samples ≈ 1.92 s) -- not seconds directly.",
                    dbl(&self.inactive, " samples"),
                ),
            ],
        }
    }

    /// One user edit; false when `id` is not this panel's.
    fn edit(&mut self, id: &str, v: &Value) -> bool {
        let standard = self.variant == AdxlVariant::Standard;
        let x = v.as_f64().unwrap_or(0.0);
        let int = v.as_i64().unwrap_or(0) as i32;
        match id {
            "adxl.range" if standard => {
                self.range.set_checked(int);
                self.range_clicked(int);
            }
            "adxl.freq" if standard => {
                self.freq.set_checked(int);
                self.freq_clicked(int);
            }
            "adxl.filter" if standard => self.filter.set_checked(int),
            "adxl.act" => self.act.set_value(x),
            "adxl.inact" if self.has_inact() => self.inact.set_value(x),
            "adxl.inactive" => self.inactive.set_value(x),
            _ => return false,
        }
        true
    }
}

// ---- The IMU panel -------------------------------------------------------------

/// LSM6 sample rate buttons.
static LSM6_ODR: [Choice; 5] = [
    choice(lsm6dsv::Odr::S100 as i32, "100 Hz", ""),
    choice(lsm6dsv::Odr::S200 as i32, "200 Hz", ""),
    choice(lsm6dsv::Odr::S400 as i32, "400 Hz", ""),
    choice(lsm6dsv::Odr::S800 as i32, "800 Hz", ""),
    choice(lsm6dsv::Odr::S1600 as i32, "1600 Hz", ""),
];
/// LSM6 accelerometer range buttons.
static LSM6_ACCEL: [Choice; 4] = [
    choice(lsm6dsv::Accel::R2g as i32, "+/- 2g", ""),
    choice(lsm6dsv::Accel::R4g as i32, "+/- 4g", ""),
    choice(lsm6dsv::Accel::R8g as i32, "+/- 8g", ""),
    choice(lsm6dsv::Accel::R16g as i32, "+/- 16g", ""),
];
/// LSM6 gyro range buttons.
static LSM6_GYRO: [Choice; 6] = [
    choice(lsm6dsv::Gyro::R125dps as i32, "+/- 125dps", ""),
    choice(lsm6dsv::Gyro::R250dps as i32, "+/- 250dps", ""),
    choice(lsm6dsv::Gyro::R500dps as i32, "+/- 500dps", ""),
    choice(lsm6dsv::Gyro::R1000dps as i32, "+/- 1000dps", ""),
    choice(lsm6dsv::Gyro::R2000dps as i32, "+/- 2000dps", ""),
    choice(lsm6dsv::Gyro::R4000dps as i32, "+/- 4000dps", ""),
];

// ---- The form --------------------------------------------------------------------

/// One panel of the page.
#[derive(Debug, Clone, PartialEq)]
pub enum Panel {
    /// The schedule.
    Schedule(Schedule),
    /// The BitTag log format.
    BitTagLog(RadioGroup),
    /// The accelerometer.
    Adxl(Adxl),
    /// The IMU: rate, accelerometer and gyro ranges.
    Lsm6([RadioGroup; 3]),
}

/// The panels in page order, by the names the probe's layout uses.
pub const PANEL_NAMES: [&str; 9] = [
    "IntervalSchedule",
    "IntervalScheduleWithPeriod",
    "StartDelaySchedule",
    "BitTagLogFormat",
    "Adxl362Standard",
    "Adxl362Wake",
    "Adxl367Wake",
    "Adxl367WakeWithType",
    "Lsm6Imu",
];

/// Which panels each tag type shows, by index into [`PANEL_NAMES`]
/// (ConfigTab's `panelsForTag()`); none for a type not listed.
pub fn panels_for_tag(tag_type: TagType) -> &'static [usize] {
    match tag_type {
        TagType::Bittag => &[0, 3, 4],
        TagType::BittagLe => &[0, 3, 5],
        TagType::Bitprestag => &[0, 5],
        TagType::Bittagng => &[0, 7],
        TagType::Uiuctag => &[0, 6],
        TagType::Compasstag => &[0],
        TagType::Prestag => &[1],
        TagType::Imutag => &[2, 8],
        _ => &[],
    }
}

impl Panel {
    /// Loads `config`; false when the panel does not apply and stays
    /// hidden (only the BitTag log panel, for a format it has no button
    /// for). A BitTag with no format adds a message to `dialogs`.
    fn load(&mut self, config: &Config, now: i64, dialogs: &mut Vec<String>) -> bool {
        match self {
            Panel::Schedule(s) => s.load(config, now),
            Panel::BitTagLog(g) => {
                if config.bittag_log != BitTagLogFmt::BittagUnspecified as i32 {
                    if g.choices.iter().any(|c| c.id == config.bittag_log) {
                        g.set_checked(config.bittag_log);
                        return true;
                    }
                } else if config.tag_type == TagType::Bittag as i32 {
                    dialogs.push(MISSING_BITTAG_LOG.to_string());
                }
                return false;
            }
            Panel::Adxl(a) => a.load(config),
            Panel::Lsm6([odr, accel, gyro]) => {
                let lsm = config.lsm6.unwrap_or_default();
                odr.set_checked(lsm.odr);
                accel.set_checked(lsm.accel_rng);
                gyro.set_checked(lsm.gyro_rng);
            }
        }
        true
    }

    /// Writes the panel's fields into `config`.
    fn store(&self, config: &mut Config, now: i64) {
        match self {
            Panel::Schedule(s) => s.store(config, now),
            // Every button is a valid format, so the store cannot fail.
            Panel::BitTagLog(g) => config.bittag_log = g.checked,
            Panel::Adxl(a) => a.store(config),
            Panel::Lsm6([odr, accel, gyro]) => {
                let mut lsm: Lsm6dsv = config.lsm6.unwrap_or_default();
                lsm.odr = odr.checked;
                lsm.accel_rng = accel.checked;
                lsm.gyro_rng = gyro.checked;
                config.lsm6 = Some(lsm);
            }
        }
    }

    /// The panel's controls.
    fn controls(&self, enabled: bool) -> Vec<Control> {
        let c = |id: &str, path, tooltip, g: &RadioGroup| Control {
            id: id.into(),
            path,
            label: "",
            tooltip,
            enabled,
            kind: ControlKind::Radio {
                choices: g.choices,
                checked: g.checked,
            },
        };
        match self {
            Panel::Schedule(s) => s.controls(enabled),
            Panel::BitTagLog(g) => vec![c(
                "bittag_log",
                &["BitTag Internal Log"],
                "Set format for BitTag log",
                g,
            )],
            Panel::Adxl(a) => a.controls(enabled),
            Panel::Lsm6([odr, accel, gyro]) => vec![
                c("lsm6.odr", &["IMU", "Sample Rate"], "", odr),
                c("lsm6.accel", &["IMU", "Accelerometer Range"], "", accel),
                c("lsm6.gyro", &["IMU", "Gyro Range"], "", gyro),
            ],
        }
    }

    /// One user edit; false when `id` is not this panel's.
    fn edit(&mut self, id: &str, v: &Value) -> bool {
        let int = v.as_i64().unwrap_or(0) as i32;
        match self {
            Panel::Schedule(s) => s.edit(id, v),
            Panel::BitTagLog(g) if id == "bittag_log" => {
                g.set_checked(int);
                true
            }
            Panel::Adxl(a) => a.edit(id, v),
            Panel::Lsm6(groups) => {
                let i = match id {
                    "lsm6.odr" => 0,
                    "lsm6.accel" => 1,
                    "lsm6.gyro" => 2,
                    _ => return false,
                };
                groups[i].set_checked(int);
                true
            }
            _ => false,
        }
    }

    /// The Config fields the panel reads and writes, as ConfigShape paths
    /// (ConfigPanel::paths); [`check_panels_against_shapes`] checks them
    /// against each tag type's default configuration.
    pub fn paths(&self) -> Vec<&'static str> {
        match self {
            Panel::Schedule(s) => match s.variant {
                ScheduleVariant::Interval => vec!["active_interval", "hibernate"],
                ScheduleVariant::IntervalWithPeriod => {
                    vec!["active_interval", "hibernate", "period"]
                }
                ScheduleVariant::StartDelay => vec!["start_delay"],
            },
            Panel::BitTagLog(_) => vec!["bittag_log"],
            Panel::Adxl(a) => {
                let mut p = vec!["adxl362.act_thresh_g", "adxl362.inactive_sec"];
                if a.has_inact() {
                    p.push("adxl362.inact_thresh_g");
                }
                if a.variant == AdxlVariant::Standard {
                    p.extend(["adxl362.range", "adxl362.freq", "adxl362.filter"]);
                }
                if a.owns_accel_type {
                    p.push("adxl362.accel_type");
                }
                p
            }
            Panel::Lsm6(_) => vec!["lsm6.odr", "lsm6.accel_rng", "lsm6.gyro_rng"],
        }
    }

    /// The panel's title, for the page.
    fn title(&self) -> &'static str {
        match self {
            Panel::Schedule(_) => "Logging Schedule",
            Panel::BitTagLog(_) => "BitTag Internal Log",
            Panel::Adxl(a) if a.variant == AdxlVariant::Standard => "Accelerometer",
            Panel::Adxl(_) => "Activity Wakeup",
            Panel::Lsm6(_) => "IMU",
        }
    }
}

/// True when `a` is `b`, or one is a field inside the other.
fn overlaps(a: &str, b: &str) -> bool {
    let under = |child: &str, parent: &str| {
        child.len() > parent.len()
            && child.starts_with(parent)
            && child.as_bytes()[parent.len()] == b'.'
    };
    a == b || under(a, b) || under(b, a)
}

/// Checks the panel table ([`panels_for_tag`]) against every tag type's
/// ConfigShape (its `default-config.json`), as `qtmonitor
/// --check-config-panels` does, with the same messages; empty when they
/// agree.
///
/// For each tag type with panels: every field a panel owns must be a Config
/// field the tag's default configuration lists, and every field it lists
/// must be owned by a panel, or lie inside or around one a panel owns. A tag
/// type with a default configuration must have panels, and one with panels
/// a default configuration. A default configuration that failed to load is
/// reported too.
///
/// Run it when a tag type, a panel or a `default-config.json` changes
/// (`design/adding-a-tag-type.md`); `tests/panels.rs` runs it.
pub fn check_panels_against_shapes() -> Vec<String> {
    use tagcore::configshape::ConfigShape;
    use tagcore::proto::DESCRIPTOR_POOL;
    let form = ConfigForm::new();
    let mut problems = Vec::new();
    let tag_types: Vec<TagType> = DESCRIPTOR_POOL
        .get_enum_by_name("TagType")
        .expect("TagType is in the descriptor pool")
        .values()
        .filter_map(|v| TagType::try_from(v.number()).ok())
        .collect();
    for &tag_type in &tag_types {
        let tag = tag_type.as_str_name();
        let ids = panels_for_tag(tag_type);
        let shape = ConfigShape::for_tag(tag_type);
        if ids.is_empty() {
            if !shape.paths().is_empty() {
                problems.push(format!("{tag}: has a default configuration but no panels"));
            }
            continue;
        }
        if shape.paths().is_empty() {
            problems.push(format!("{tag}: has panels but no default configuration"));
            continue;
        }
        let mut owned = Vec::new();
        for &id in ids {
            for path in form.panels[id].paths() {
                owned.push(path);
                if !ConfigShape::is_config_path(path) {
                    problems.push(format!(
                        "{tag}: panel {} owns \"{path}\", which is not a Config field",
                        PANEL_NAMES[id]
                    ));
                } else if !shape.paths().contains(path) {
                    problems.push(format!(
                        "{tag}: panel {} edits {path}, which the tag's default configuration does not list",
                        PANEL_NAMES[id]
                    ));
                }
            }
        }
        for path in shape.paths() {
            if !owned.iter().any(|o| overlaps(path, o)) {
                problems.push(format!(
                    "{tag}: no panel edits {path}, which the tag's default configuration lists"
                ));
            }
        }
    }
    problems.extend(ConfigShape::load_errors().iter().cloned());
    problems
}

/// The page of panels: every panel built once, the tag type choosing which
/// are shown, and whether they can be edited.
///
/// The panels keep their controls' values between loads, as qtmonitor's
/// widgets do, so a field a load leaves alone (a zero PresTag period) keeps
/// what the previous document showed.
#[derive(Debug, Clone, PartialEq)]
pub struct ConfigForm {
    /// All panels, in [`PANEL_NAMES`] order.
    panels: Vec<Panel>,
    /// The shown panels' indices, in page order.
    shown: Vec<usize>,
    /// The shown panels can be edited.
    enabled: bool,
}

impl Default for ConfigForm {
    fn default() -> Self {
        Self::new()
    }
}

impl ConfigForm {
    /// Every panel, built once, none shown.
    pub fn new() -> Self {
        ConfigForm {
            panels: vec![
                Panel::Schedule(Schedule::new(ScheduleVariant::Interval)),
                Panel::Schedule(Schedule::new(ScheduleVariant::IntervalWithPeriod)),
                Panel::Schedule(Schedule::new(ScheduleVariant::StartDelay)),
                Panel::BitTagLog(RadioGroup::new(&BITTAG_LOG)),
                Panel::Adxl(Adxl::new(AdxlVariant::Standard, false)),
                Panel::Adxl(Adxl::new(AdxlVariant::Wake362, false)),
                Panel::Adxl(Adxl::new(AdxlVariant::Wake367, false)),
                Panel::Adxl(Adxl::new(AdxlVariant::Wake367, true)),
                Panel::Lsm6([
                    RadioGroup::new(&LSM6_ODR),
                    RadioGroup::new(&LSM6_ACCEL),
                    RadioGroup::new(&LSM6_GYRO),
                ]),
            ],
            shown: Vec::new(),
            enabled: false,
        }
    }

    /// Shows the panels `tag_type` selects and loads `config` into them;
    /// returns any message a panel raised. `now` is the clock, seconds.
    pub fn load(&mut self, config: &Config, tag_type: TagType, now: i64) -> Vec<String> {
        let mut dialogs = Vec::new();
        let selected = panels_for_tag(tag_type);
        self.shown.clear();
        for (i, panel) in self.panels.iter_mut().enumerate() {
            if selected.contains(&i) && panel.load(config, now, &mut dialogs) {
                self.shown.push(i);
            }
        }
        dialogs
    }

    /// Hides every panel (no document).
    pub fn clear(&mut self) {
        self.shown.clear();
    }

    /// What Start would send: `base` with `tag_type`, each shown panel's
    /// fields written over it. `now` is the clock, seconds.
    pub fn collect(&self, base: &Config, tag_type: TagType, now: i64) -> Config {
        let mut config = base.clone();
        config.tag_type = tag_type as i32;
        for &i in &self.shown {
            self.panels[i].store(&mut config, now);
        }
        config
    }

    /// Whether the shown panels can be edited.
    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }

    /// The shown panels' names, in page order (the probe's `layout`).
    pub fn shown_names(&self) -> Vec<&'static str> {
        self.shown.iter().map(|&i| PANEL_NAMES[i]).collect()
    }

    /// Every shown control.
    pub fn controls(&self) -> Vec<Control> {
        self.shown
            .iter()
            .flat_map(|&i| self.panels[i].controls(self.enabled))
            .collect()
    }

    /// The probe's fingerprint of every shown control, sorted. Group boxes
    /// and labels, which a load never changes, are left out.
    pub fn fingerprint(&self) -> Vec<String> {
        let mut lines: Vec<String> = self
            .controls()
            .iter()
            .flat_map(Control::fingerprint)
            .collect();
        lines.sort();
        lines
    }

    /// The form for the page: each shown panel with its controls.
    pub fn view(&self) -> Value {
        json!(self.shown.iter().map(|&i| {
            let p = &self.panels[i];
            json!({ "name": PANEL_NAMES[i], "title": p.title(),
                "controls": p.controls(self.enabled).iter().map(Control::view).collect::<Vec<_>>() })
        }).collect::<Vec<_>>())
    }

    /// One user edit of control `id` (see [`Control::id`]) to `value`: a
    /// number, a bool for a check box, a choice id for radio buttons, or
    /// seconds since the epoch for a date and time. Returns false when no
    /// shown, enabled control has that id.
    pub fn edit(&mut self, id: &str, value: &Value) -> bool {
        if !self.enabled {
            return false;
        }
        let shown = self.shown.clone();
        shown.into_iter().any(|i| self.panels[i].edit(id, value))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// QString::number(v, 'f', n): halves away from zero, exact binary value.
    #[test]
    fn qt_round_matches_qt() {
        assert_eq!(qt_round(0.125, 2), 0.13);
        assert_eq!(qt_round(2.5, 0), 3.0);
        assert_eq!(qt_round(-2.5, 0), -3.0);
        assert_eq!(qt_round(0.015625, 2), 0.02);
        assert_eq!(qt_round(1.005, 2), 1.0); // 1.005 is just below in binary
        assert_eq!(qt_round(9.999, 2), 10.0);
        assert_eq!(qt_round(5.1, 2), 5.1);
    }

    /// The hour rounding of the schedule's boxes.
    #[test]
    fn hours_round_like_hourtime() {
        assert_eq!(ceil_hour(3601), 7200);
        assert_eq!(ceil_hour(-1), 0);
        assert_eq!(floor_hour(-1), -3600);
        assert_eq!(round_epoch(-100, 60), 0);
        assert_eq!(round_epoch(61, 60), 120);
    }
}
