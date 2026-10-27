// The form spec: what the engine would send as FormSpec. Pure data, taken
// from qtmonitor's panels (host/applications/qtmon/tagconfiguration) and the
// tags' default-config.json files. Nothing here is UI code.

const ADXL_RANGES = [
  { value: "R2G", label: "+/- 2g" }, { value: "R4G", label: "+/- 4g" }, { value: "R8G", label: "+/- 8g" }];
const ADXL_RATES = [
  { value: "S12_5", label: "12.5 Hz", tooltip: "12.5 samples/second" },
  { value: "S25", label: "25 Hz", tooltip: "25 samples/second" },
  { value: "S50", label: "50 Hz", tooltip: "50 samples/second" },
  { value: "S100", label: "100 Hz", tooltip: "100 samples/second" },
  { value: "S200", label: "200 Hz", tooltip: "200 samples/second" },
  { value: "S400", label: "400 Hz", tooltip: "400 samples/second" }];
const ADXL_FILTERS = [
  { value: "AAquarter", label: "1/4 Sample Rate", tooltip: "filter cutoff -- 1/4 sample rate" },
  { value: "AAhalf", label: "1/2 Sample Rate", tooltip: "filter cutoff -- 1/2 sample rate" }];

const START = { id: "start", kind: "whenTime", label: "Start", tooltip: "Set data collection starting conditions.",
  options: [{ value: "now", label: "Immediately", tooltip: "Start data collection as soon as possible." },
            { value: "at", label: "On", tooltip: "Start data collection at a specific time." }],
  timeTooltip: "Programmed date/time to start data collection (UTC, whole hours)" };
const END = { id: "end", kind: "whenTime", label: "Run", tooltip: "Set data collection ending conditions.",
  options: [{ value: "never", label: "Indefinitely", tooltip: "Collect data for as long as possible, until either battery or storage runs out." },
            { value: "at", label: "Until", tooltip: "Stop collecting data at a specific time." }],
  timeTooltip: "Stop data collection at this date/time (UTC, whole hours)" };
const HIBERNATE = { id: "hibernate", kind: "intervals", label: "Hibernate", rows: 2,
  tooltip: "Hibernate between the start and end. Unchecked, the row is not used.",
  startTooltip: "Start hibernating at this date/time (UTC, whole hours)",
  endTooltip: "Stop hibernating at this date/time (UTC, whole hours). The tag checks once an hour, on the hour, whether to stop hibernating." };

window.FORM_SPEC = {
  panels: {
    IntervalSchedule: { title: "Logging Schedule", fields: [START, END, HIBERNATE] },
    IntervalScheduleWithPeriod: { title: "Logging Schedule", fields: [START, END, HIBERNATE,
      { id: "period", kind: "number", label: "Period", units: "Seconds", tooltip: "Data sample period" }] },
    StartDelaySchedule: { title: "Logging Schedule", fields: [
      { id: "start_delay", kind: "number", label: "Start Delay", units: "minutes", tooltip: "Start delay in minutes -- rounded to next minute" }] },
    BitTagLogFormat: { title: "BitTag Log Format", fields: [
      { id: "bittag_log", kind: "enum", label: "Log format", tooltip: "Set format for BitTag log", options: [
        { value: "BITTAG_BITPERSEC", label: "Activity Bit Per Second" },
        { value: "BITTAG_BITSPERMIN", label: "Activity Count Per Minute" },
        { value: "BITTAG_BITSPERFOURMIN", label: "Activity Count Per Four Minutes" },
        { value: "BITTAG_BITSPERFIVEMIN", label: "Activity Count Per Five Minutes" }] }] },
    Adxl362Standard: { title: "Accelerometer", fields: [
      { id: "range", kind: "enum", label: "Range", tooltip: "Set Accelerometer Range", options: ADXL_RANGES },
      { id: "freq", kind: "enum", label: "Sample Rate", tooltip: "Set Accelerometer Sample Rate", options: ADXL_RATES },
      { id: "filter", kind: "enum", label: "Filter", tooltip: "Set Accelerometer Anti-Alias Filter", options: ADXL_FILTERS },
      { id: "act_thresh_g", kind: "number", label: "Active Threshold", units: "g", tooltip: "Wakeup threshold" },
      { id: "inact_thresh_g", kind: "number", label: "Inactive Threshold", units: "g", tooltip: "Sleep threshold" },
      { id: "inactive_sec", kind: "number", label: "Inactivity", units: "sec", tooltip: "Time below wakeup threshold to become inactive" }] },
    Adxl362Wake: { title: "Activity Wakeup", fields: [
      { id: "act_thresh_g", kind: "number", label: "Active Threshold", units: "g", tooltip: "ADXL362 wake-up active threshold in g" },
      { id: "inact_thresh_g", kind: "number", label: "Inactive Threshold", units: "g", tooltip: "ADXL362 wake-up inactive threshold in g" },
      { id: "inactive_sec", kind: "number", label: "Inactivity Samples", units: "samples", tooltip: "Samples below inactive threshold at 6 Hz" }] },
    Adxl367Wake: { title: "Activity Wakeup", fields: [
      { id: "act_thresh_g", kind: "number", label: "Wakeup Threshold", units: "g", tooltip: "ADXL367 wake-up threshold in g" },
      { id: "inactive_sec", kind: "number", label: "Inactivity Samples", units: "samples",
        tooltip: "Samples below the inactivity threshold before the ADXL367 declares inactivity. Counted at its 6.25 Hz wake rate, so one sample is ~160 ms -- not seconds directly." }] },
    Lsm6Imu: { title: "IMU", fields: [
      { id: "odr", kind: "enum", label: "Sample Rate", options: ["S100", "S200", "S400", "S800", "S1600"].map((v) => ({ value: v, label: v.slice(1) + " Hz" })) },
      { id: "accelRng", kind: "enum", label: "Accelerometer Range", options: ["R2G", "R4G", "R8G", "R16G"].map((v) => ({ value: v, label: "+/- " + v.slice(1).toLowerCase() })) },
      { id: "gyroRng", kind: "enum", label: "Gyro Range", options: ["R125dps", "R250dps", "R500dps", "R1000dps", "R2000dps", "R4000dps"].map((v) => ({ value: v, label: "+/- " + v.slice(1) })) }] },
  },
  // configtab.cpp panelsForTag(): Adxl367WakeWithType renders as Adxl367Wake
  // (its extra field, accel_type, is passed through, not shown).
  panelsForTag: {
    BITTAG: ["IntervalSchedule", "BitTagLogFormat", "Adxl362Standard"],
    BITTAG_LE: ["IntervalSchedule", "BitTagLogFormat", "Adxl362Wake"],
    BITPRESTAG: ["IntervalSchedule", "Adxl362Wake"],
    BITTAGNG: ["IntervalSchedule", "Adxl367Wake"],
    UIUCTAG: ["IntervalSchedule", "Adxl367Wake"],
    COMPASSTAG: ["IntervalSchedule"],
    PRESTAG: ["IntervalScheduleWithPeriod"],
    IMUTAG: ["StartDelaySchedule", "Lsm6Imu"],
  },
};

// The tags' default-config.json files (embedded/proto-c/*/default-config.json).
const NOHIB = [{ start_epoch: 0, end_epoch: 0 }, { start_epoch: 0, end_epoch: 0 }];
const ALWAYS = { start_epoch: 0, end_epoch: 2147483647 };
window.DEFAULT_CONFIGS = {
  BITTAG: { tag_type: "BITTAG", active_interval: ALWAYS, hibernate: NOHIB, bittag_log: "BITTAG_BITSPERFIVEMIN",
            adxl362: { range: "R4G", freq: "S50", filter: "AAquarter", act_thresh_g: 0.5, inact_thresh_g: 0.5, inactive_sec: 0.5 } },
  BITTAG_LE: { tag_type: "BITTAG_LE", active_interval: ALWAYS, hibernate: NOHIB, bittag_log: "BITTAG_BITSPERFIVEMIN",
               adxl362: { act_thresh_g: 0.5, inact_thresh_g: 0.35, inactive_sec: 3 } },
  BITPRESTAG: { tag_type: "BITPRESTAG", active_interval: ALWAYS, hibernate: NOHIB, adxl362: { act_thresh_g: 0.5, inact_thresh_g: 0.35, inactive_sec: 3 } },
  BITTAGNG: { tag_type: "BITTAGNG", active_interval: ALWAYS, hibernate: NOHIB, adxl362: { accel_type: "AdxlType_367", act_thresh_g: 0.5, inactive_sec: 6 } },
  UIUCTAG: { tag_type: "UIUCTAG", active_interval: ALWAYS, hibernate: NOHIB, adxl362: { act_thresh_g: 0.5, inactive_sec: 10 } },
  COMPASSTAG: { tag_type: "COMPASSTAG", active_interval: ALWAYS, hibernate: NOHIB },
  PRESTAG: { tag_type: "PRESTAG", active_interval: ALWAYS, hibernate: NOHIB, period: 60 },
  IMUTAG: { tag_type: "IMUTAG", start_delay: 1, lsm6: { odr: "S100", accelRng: "R4G", gyroRng: "R1000dps" } },
};
