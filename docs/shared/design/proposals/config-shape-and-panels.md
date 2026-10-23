---
type: proposal
status: proposed
summary: qtmonitor's configuration screen becomes fixed panels chosen by tag type, and a tagcore ConfigShape read from each tag's default config replaces the generated visibility struct, drives one merge for restore and tag-start, and cross-checks the panels; tag-reported presence is a deferred later stage.
---

# Configuration Shape and Panels

**Proposal.** Rebuild qtmonitor's configuration screen from **fixed panels,
built once and selected by tag type**. Back it with a small tagcore class,
**`ConfigShape`**: the set of `Config` field paths a tag supports, read from the
tag's checked-in `default-config.json`. Each part has its own job:

| Part | Decides |
| --- | --- |
| the panels selected for the tag type | which widgets are visible |
| each panel's binding table | which `Config` fields are read into widgets and written back |
| `ConfigShape` | which fields a restore or `tag-start --merge` may copy, and whether the panels agree with the firmware's default configs |

This replaces the build-time visibility generator, the `ConfigFieldVisibility`
struct, per-widget visibility checks, run-time widget rebuilding and two
disagreeing merges. The proto, nanopb, the firmware and the bench are
untouched.

The work lands in two steps: **stage 1** (`ConfigShape`, the merge, the
generator removed) and **stage 1b** (the panels). **Stage 2**, in which the tag
reports its own shape through proto3 `optional` fields, is described at the end
and deferred.

## The problem today

proto3 does not record whether a zero-valued scalar was set, so a decoded
`Config` cannot say which fields a tag supports. qtmonitor works this out at
build time instead. `cmake/GenerateConfigVisibility.cmake` regex-scans every
`embedded/proto-c/*-proto-c/default-config.json` and emits a table of booleans
per tag type (`ConfigFieldVisibility`). Four modules in
`host/applications/qtmon/tagconfiguration/` (`Schedule`, `BitTagLogTab`,
`Adxl362Config`, `Lsm6dsvConfig`) consult it widget by widget.

**A field is described in about seven places:**

1. `proto/tagdata.proto`
2. the `ConfigFieldVisibility` struct
3. the generator's field list
4. the generator's key regex
5. the owning module's two-argument `SetConfig()` and `GetConfig()`
6. that module's one-argument `SetConfig()`, which guesses visibility from
   values (nothing calls it, but `ConfigInterface` requires it)
7. `mergeRestoredConfig()` in `configtab.cpp`

The firmware comes on top of that.

**The layout changes at run time.**

- `Schedule::SetConfig()` deletes and recreates its `Hibernate` rows on every
  call, although `Config.hibernate` has `max_count:2`
  (`embedded/proto-c/default-options/tagdata.options`) and every firmware
  reports two.
- `Adxl362Config::SetConfig()` rewrites its own title, labels, ranges, suffixes
  and tooltips through a three-way `if/else` on tag type
  (`usesAdxl367WakeConfig`, `usesWakeSampleConfig`).

**Defects:**

- **The generator ignores nesting.** It matches the key names `range`, `freq`,
  `filter`, `odr` and `channels` anywhere in the file. It is correct today only
  because no two sub-messages share a key name.
- **The two merges disagree.** qtmonitor's restore skips zero values from the
  file. `tag-start --merge` uses `MergeFrom`, which also skips zeros but copies
  fields the tag does not support. Neither can set a value back to zero.
- **What gets written depends on what is on screen.** `Adxl362Config::GetConfig()`
  writes sample rate, range, filter and inactive threshold only if the widget
  `isVisible()` (`adxl362config.cpp:132-157`). `isVisible()` is false when any
  ancestor is hidden, and `QTabWidget` hides the pages that are not selected.
  Start, Save and Restore sit below the tabs. So, as far as can be read from
  the code, a range changed on the Sensors tab is not sent if the user goes back
  to the Schedule tab before pressing Start. The probe (rollout step 0) confirmed
  it on 2026-10-23, in 69 cases; see
  `host/applications/qtmon/test/expected-differences.md`.

## Stage 1: `ConfigShape`

### The class

```cpp
// host/libraries/tagcore/configshape.h

/// The Config fields one tag type supports, as dotted field paths.
class ConfigShape
{
public:
  /// Shape of a tag type, from its embedded default config(s); the empty
  /// shape for a type with none. Built once, lives for the program.
  static const ConfigShape &forTag(TagType tag_type);

  /// Shape of a JSON Config text: the keys it actually contains, zeros
  /// included. Used to tell what a restore or --merge file sets.
  static std::optional<ConfigShape> fromJson(std::string_view json,
                                             std::string *error);

  /// True when @p path names a supported field. @p path must name a real
  /// Config field (see "Typos" below).
  bool has(std::string_view path) const;

  /// Every path in the shape, for the panel cross-check.
  const std::set<std::string, std::less<>> &paths() const;

  /// Copy into @p target every field that is in this shape and in
  /// @p overlay_shape, from @p overlay.
  void merge(Config &target, const Config &overlay,
             const ConfigShape &overlay_shape) const;

private:
  std::set<std::string, std::less<>> paths_;
};
```

It is an ordinary class holding a set of strings. Three details of the
declaration:

- `std::less<>` lets `find()` take a `std::string_view` or a string literal
  without building a temporary `std::string`.
- `forTag()` returns a reference to an object that lives until exit.
- `std::optional` is empty when the JSON does not parse, and the reason comes
  back through `error`.

### Where the defaults come from

The sources are the hand-written, checked-in
`embedded/proto-c/*-proto-c/default-config.json` files. Today there are nine,
covering eight tag types, because `prestag-proto-c` and `prestagraw-proto-c`
are both `PRESTAG`. No new source files are added. The firmware already depends
on these files: `config-gen.py` turns each into the default configuration
compiled into that tag's image, rejecting any key that is not in the proto, and
BitTag and BitTagNG report that default in IDLE.

**Build time.** tagcore's `CMakeLists.txt` finds the files with the glob
qtmonitor uses today. It copies each file's **text** into a single generated
source file in the build tree:

```cmake
file(GLOB DEFAULT_CONFIG_JSONS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/embedded/proto-c/*-proto-c/default-config.json")
```

```cpp
// <build>/host/libraries/tagcore/defaultconfigs.cc  (generated)
const EmbeddedDefault kEmbeddedDefaults[] = {
  {"bittag-ng-proto-c", R"json({ "tag_type": "BITTAGNG", ... })json"},
  {"imutag-proto-c",    R"json({ "tag_type": "IMUTAG", ... })json"},
  ...
};
```

This step knows nothing about fields. Editing a JSON file rebuilds tagcore, and
`CONFIGURE_DEPENDS` picks up a new tag's file on the next build. A packaged
qtmonitor carries the texts inside the binary.

**Run time.** `forTag()` keeps the shapes in a function-local `static`, which
C++ initialises the first time execution reaches it, thread-safely, and never
again:

```cpp
const ConfigShape &ConfigShape::forTag(TagType tag_type)
{
  static const std::map<TagType, ConfigShape> shapes = buildFromEmbeddedDefaults();
  static const ConfigShape empty;
  auto it = shapes.find(tag_type);
  return it != shapes.end() ? it->second : empty;
}
```

`buildFromEmbeddedDefaults()` takes each embedded text in turn:

- it parses the text into a `google::protobuf::Struct`, protobuf's generic JSON
  value, which unlike `Config` keeps every key, zeros included;
- it reads `tag_type` with the generated `TagType_Parse()`;
- it walks the keys into that tag type's path set.

The walk applies these rules:

- a key holding an object records its own path and descends, so `adxl362` and
  `adxl362.range` are both recorded;
- a key holding an array (`hibernate`) records its own path and descends into
  the first element;
- each key is resolved against the `Config` descriptor at its level, by
  `FindFieldByName()`, falling back to a match on each field's `json_name()`.
  The canonical snake_case name is stored, so IMUTag's `accelRng` becomes
  `lsm6.accel_rng`;
- values are never interpreted, only used to see whether to descend;
- `tag_type` selects the entry and is not itself a path;
- two files with the same tag type are unioned.

Because each key is resolved at its own level, `range` is accepted inside
`adxl362` and would be rejected inside `lsm6`. Today's generator cannot make
that distinction.

The cost is nine files of under 1 KB, paid once.

**A bad default config** is a build defect. `config_shape_check` (test
T1) parses every embedded text and fails on it. In qtmonitor, the
parse logs the file and key and skips that file, so one bad default cannot stop
the program starting. The tag type it described gets the empty shape, and so no
panels (see stage 1b).

The alternative is to freeze the shapes as a checked-in table. That would cut
the build's last tie to `embedded/proto-c/`, but it would put a second copy of
each tag's field list in the tree.

### Examples of `has()`

The shapes these calls see:

| Tag type | Paths |
| --- | --- |
| `BITTAG` | `active_interval…`, `hibernate…`, `bittag_log`, `adxl362`, `adxl362.range`, `.freq`, `.filter`, `.act_thresh_g`, `.inact_thresh_g`, `.inactive_sec` |
| `BITTAG_LE` | `active_interval…`, `hibernate…`, `bittag_log`, `adxl362`, `adxl362.act_thresh_g`, `.inact_thresh_g`, `.inactive_sec` |
| `BITPRESTAG` | `active_interval…`, `hibernate…`, `adxl362`, `adxl362.act_thresh_g`, `.inact_thresh_g`, `.inactive_sec` |
| `BITTAGNG` | `active_interval…`, `hibernate…`, `adxl362`, `adxl362.accel_type`, `.act_thresh_g`, `.inactive_sec` |
| `UIUCTAG` | `active_interval…`, `hibernate…`, `adxl362`, `adxl362.act_thresh_g`, `.inactive_sec` |
| `COMPASSTAG` | `active_interval…`, `hibernate…` |
| `PRESTAG` | `active_interval…`, `hibernate…`, `period` |
| `IMUTAG` | `start_delay`, `lsm6`, `lsm6.odr`, `lsm6.accel_rng`, `lsm6.gyro_rng` |

```cpp
const ConfigShape &ng = ConfigShape::forTag(TagType::BITTAGNG);
ng.has("adxl362.accel_type");    // true
ng.has("adxl362.range");         // false: BitTagNG fixes the range

const ConfigShape &imu = ConfigShape::forTag(TagType::IMUTAG);
imu.has("lsm6.accel_rng");       // true, although the JSON says "accelRng"
imu.has("active_interval");      // false: IMUTag schedules by start_delay

const ConfigShape &pres = ConfigShape::forTag(TagType::PRESTAG);
pres.has("period");              // true, from either PRESTAG default
```

**Typos.** A misspelt path must not quietly answer `false`. So `has()`
resolves its argument against the `Config` descriptor on first use, and caches
the result:

```cpp
shape.has("adxl362.frq");   // not a Config field: aborts in debug builds,
                            // logs an error and returns false in release
```

An unknown path is a programming error, which is different from a field this
tag does not support.

### One merge for restore and `tag-start --merge`

A restore needs to know which fields the file actually sets. It gets that from
the same JSON walk, and copies only the fields the tag supports *and* the file
sets:

```cpp
std::string error;
auto file_shape = ConfigShape::fromJson(text, &error);
Config file_config;   // the same text, parsed into Config
...
shape.merge(current_config_, file_config, *file_shape);
```

| Tag | File | Result |
| --- | --- | --- |
| `BITTAGNG` | `{"adxl362": {"act_thresh_g": 0.3}}` | Copies `act_thresh_g`. `inactive_sec` keeps the tag's value. |
| `PRESTAG` | `{"period": 0}` | Sets `period` to 0. Today this is skipped. |
| `BITTAG_LE` | `{"adxl362": {"freq": "S400"}}` | Nothing is copied. BitTag LE has no `freq`. |
| `IMUTAG` | a full file saved from a PresTag | Nothing is copied. |

`merge()` never copies `tag_type`. `hibernate` is copied element by element, up
to the tag's own count. `tag-start --merge` makes the same call, so a merged
file can set a value to zero and can no longer add a field the tag lacks.

### Landing stage 1 before the panels

Stage 1 can ship on its own without rewriting the modules. A twenty-line
adapter, `visibilityFromShape(const ConfigShape &)`, fills the existing
`ConfigFieldVisibility` from the shape, and `ConfigTab::SetConfig()` calls it in
place of `configFieldVisibilityForTag()`. The generator and its CMake rule are
deleted. The modules are unchanged until stage 1b removes them, the adapter and
the struct together.

## Stage 1b: fixed panels selected by tag type

### The idea

Every panel is built once, when `ConfigTab` is constructed, with its title,
labels, ranges, suffixes and tooltips fixed in its `.ui` file. The tag type
selects which panels are shown. A panel is shown or hidden **as a whole**: one
selected for a tag matches that tag exactly, so it never hides its own widgets.

The Schedule and Sensors tabs go. A tag now shows one to three panels, so they
are stacked on a single scrolling page, in the order listed below: schedule
first, then data, then sensors.

### The panels

| Panel | Owns | Notes |
| --- | --- | --- |
| Interval schedule | `active_interval`, `hibernate` | start now / on a date, run indefinitely / until, two fixed hibernation rows |
| Start delay | `start_delay` | |
| Sampling period | `period` | PresTag's sampling period, kept separate so the interval schedule is the same for every tag that uses it |
| BitTag log | `bittag_log` | |
| ADXL362 | `adxl362.range`, `.freq`, `.filter`, `.act_thresh_g`, `.inact_thresh_g`, `.inactive_sec` | the threshold step follows the range; the inactivity step follows the sample rate |
| ADXL362 wake | `adxl362.act_thresh_g`, `.inact_thresh_g`, `.inactive_sec` | thresholds in g for the fixed 4 g range; inactivity as 1-255 samples |
| ADXL367 wake | `adxl362.act_thresh_g`, `.inactive_sec`; passes through `adxl362.accel_type` | threshold 0.2-0.7 g in 0.05 g steps; inactivity as 3-12 samples at 6.25 Hz |
| LSM6 IMU | `lsm6.odr`, `.accel_rng`, `.gyro_rng` | |

The three ADXL panels write the same `adxl362` fields, because the firmware
reuses that message with different meanings. Each meaning now has its own panel
with fixed labels, instead of one panel relabelling itself. That reuse is
documented in `tagconfiguration/README.md` and in each family's `config.c`.
The panel's tooltips carry it into the UI.

### Tag type to panels

```cpp
// qtmon/tagconfiguration/configpanels.cpp -- UI policy, so it lives in qtmonitor
static const std::map<TagType, std::vector<PanelId>> kPanelsForTag = {
  {TagType::BITTAG,     {IntervalSchedule, BitTagLog, Adxl362}},
  {TagType::BITTAG_LE,  {IntervalSchedule, BitTagLog, Adxl362Wake}},
  {TagType::BITPRESTAG, {IntervalSchedule, Adxl362Wake}},
  {TagType::BITTAGNG,   {IntervalSchedule, Adxl367Wake}},
  {TagType::UIUCTAG,    {IntervalSchedule, Adxl367Wake}},
  {TagType::COMPASSTAG, {IntervalSchedule}},
  {TagType::PRESTAG,    {IntervalSchedule, SamplingPeriod}},
  {TagType::IMUTAG,     {StartDelay, Lsm6}},
};
```

A tag type with no entry gets no panels and an inactive Configuration tab,
which is what an unknown tag gets today.

### `ConfigPanel` and binding tables

```cpp
/// One fixed group of configuration controls owning a set of Config fields.
class ConfigPanel : public QWidget
{
public:
  /// Fields this panel reads and writes.
  virtual std::vector<std::string_view> paths() const = 0;
  /// Fields the tag reports that this panel keeps but never edits.
  virtual std::vector<std::string_view> passThrough() const { return {}; }

  /// Fill the widgets from @p config.
  virtual void load(const Config &config) = 0;
  /// Write every field in paths() into @p config; leave all others alone.
  virtual void store(Config &config) const = 0;
};
```

Most panels implement none of the three methods themselves. They declare a
**binding table**, and a shared base class, `BoundPanel`, derives `paths()`,
`load()` and `store()` from it:

```cpp
// Adxl362WakePanel constructor, after ui.setupUi(this)
bind(ui.actThresh,   "adxl362.act_thresh_g");
bind(ui.inactThresh, "adxl362.inact_thresh_g");
bind(ui.inactive,    "adxl362.inactive_sec");
```

```cpp
void BoundPanel::load(const Config &config)
{
  for (const Binding &b : bindings_)
    b.load(config);           // read the field at b.path into b.widget
}

void BoundPanel::store(Config &config) const
{
  for (const Binding &b : bindings_)
    b.store(config);          // write b.widget's value to the field at b.path
}
```

A `Binding` resolves its path through the `Config` descriptor once, when it is
created, and reads and writes the field through protobuf reflection. A path
that does not resolve, or a widget kind that does not suit the field, aborts at
construction, so a bad row cannot survive the first run. Only three pairings
are needed:

| Widget | Field type | Read / write |
| --- | --- | --- |
| `PBEnumGroup` | enum | `setCheckedId()` / `checkedId()`, ignoring an id the enum does not define |
| `QDoubleSpinBox` | `float` | `setValue()` / `value()` |
| `QSpinBox` | integer | `setValue()` / `value()` |

These panels are pure binding tables: ADXL362, ADXL362 wake, ADXL367 wake,
LSM6 IMU and Sampling period. BitTag log binds its one field and adds the
warning below. ADXL362 adds its two signal
connections for the dependent steps. ADXL367 wake declares `adxl362.accel_type`
as pass-through.

**Hand-written `load()`/`store()`:**

- **Interval schedule.** The date logic stays: "start now" stores 0, "run
  indefinitely" stores `INT32_MAX`, times round to the hour. Its two
  `Hibernate` rows are placed in `.ui` once, not rebuilt.
- **Start delay.** The widget shows minutes, and the panel stores minutes + 1.
  The offset is part of the protocol: proto3 cannot send a 0, so IMUTag's
  `writeConfig()` reads the field as minutes + 1, starts the run
  `(start_delay - 1) * 60` seconds after the start command, and turns a 0 into
  1. Changing the offset would change the meaning of every IMUTag already
  built. The panel's comment says so.
- **Sampling period** stays a binding, with the widget's minimum at 1. PresTag's
  `writeConfig()` rejects a period of 0.
- **BitTag log.** It keeps today's warning when a `BITTAG` reports no log
  format.

### `ConfigTab` afterwards

```text
ConfigTab()
  build every panel once; add each to the page; hide it

SetConfig(cfg)
  current_config_ = cfg
  shape_  = &ConfigShape::forTag(cfg.tag_type())
  shown_  = kPanelsForTag[cfg.tag_type()]
  show shown_, hide the rest
  for p in shown_: p->load(cfg)

GetConfig(cfg)
  cfg = current_config_                 // fields no panel owns pass through
  for p in shown_: p->store(cfg)        // regardless of what is on screen

Restore   shape_->merge(current_config_, file, ConfigShape::fromJson(text)); SetConfig(...)
Start     GetConfig() then tag->Start(), as today
```

`store()` writes every field the panel owns, whatever is on screen, so the
`isVisible()` defect cannot recur.

### The cross-check

The panels say what qtmonitor edits for a tag type. The shape says what that
tag's default config lists. `ConfigTab::checkPanelsAgainstShapes()` compares
them for every tag type. It requires that:

- every path a selected panel owns is in the shape, so no panel edits a field
  the tag does not have. Pass-through paths may be absent: UIUCTag shares the
  ADXL367 wake panel but its default lists no `accel_type`;
- every path in the shape is covered by a selected panel, meaning owned or
  passed through by it, or an ancestor or descendant of such a path, so the
  default config lists no field that qtmonitor silently ignores;
- every tag type with a default config has a `kPanelsForTag` entry, and every
  entry has a default config.

It runs as `qtmonitor --check-config-panels`, which builds the panels without
showing them (`QT_QPA_PLATFORM=offscreen`), prints each mismatch and exits
non-zero. Debug builds also run it at startup. With today's default configs,
the table above passes.

### What this does to field and tag recipes

**Adding a field:**

1. add it to `proto/tagdata.proto`;
2. add the key to the default config of each tag that supports it;
3. handle it in that firmware's `readConfig()`/`writeConfig()`;
4. add a widget and one `bind()` row to the panel that owns it.

If step 2 or 4 is missed, the cross-check fails.

**Adding a tag type:** write its default config and add one `kPanelsForTag`
row, usually reusing existing panels. If the tag reuses a field with a new
meaning, add a panel for that meaning rather than branching inside an existing
one.

## How the code structure changes

Before:

```text
build:  GenerateConfigVisibility.cmake ──regex──▶ struct of 20 bools per tag

ConfigTab
  Schedule, BitTagLogTab, Adxl362Config, Lsm6dsvConfig   (always present)
  SetConfig: each module gets the struct, hides/shows its own widgets,
             relabels itself by tag type, rebuilds hibernation rows
  GetConfig: each module writes fields whose bit is set (and, for ADXL,
             whose widget isVisible())
  Restore:   mergeRestoredConfig()             hand-written, skips zeros
tag-start:   MergeFrom                         skips zeros, adds anything
```

After:

```text
build:  tagcore CMakeLists.txt (glob, copy text) ──▶ defaultconfigs.cc

tagcore   ConfigShape::forTag / fromJson / has / merge
qtmonitor ConfigTab
            eight ConfigPanels, built once
            kPanelsForTag: TagType → panels
            SetConfig: show the tag's panels; load each
            GetConfig: store each shown panel
            Restore:   ConfigShape::merge
            --check-config-panels: panels vs shapes
tag-start   ConfigShape::merge
```

File by file:

| File | Stage | Change |
| --- | --- | --- |
| `cmake/GenerateConfigVisibility.cmake` | 1 | deleted |
| `host/applications/qtmon/CMakeLists.txt` | 1, 1b | generator rule removed; panel sources added, module sources removed |
| `host/libraries/tagcore/configshape.{h,cc}` | 1 | new |
| `host/libraries/tagcore/CMakeLists.txt` | 1 | globs the default configs into `defaultconfigs.cc`, about 20 lines |
| `host/libraries/tagcore/test/config_shape_check.cc` | 1 | new standalone check (T1) |
| `qtmon/tagconfiguration/configclock.{h,cpp}` | 0 | one clock for the schedule code |
| `qtmon/configprobe.{h,cpp}` | 0 | the probe |
| `host/applications/qtmon/test/` | 0 | `config_probe_cases.cc` (behind `BUILD_QTMON_CHECKS`), `cases/`, `probe.sh`, `compare.py`, `golden/`, `expected-differences.md`, README |
| `host/commandline/tag-start.cc` | 1 | `--merge` uses `ConfigShape::merge()`; the stale comment goes |
| `qtmon/tagconfiguration/configtab.cpp` | 1, 1b | stage 1: adapter in place of the generated table, `mergeRestoredConfig()` deleted. Stage 1b: holds panels and `kPanelsForTag` |
| `qtmon/tagconfiguration/configpanel.{h,cpp}` | 1b | new: `ConfigPanel`, `BoundPanel`, `Binding` |
| `qtmon/tagconfiguration/configpanels.cpp` | 1b | new: `kPanelsForTag` and `checkPanelsAgainstShapes()` |
| `qtmon/tagconfiguration/*panel.{h,cpp,ui}` | 1b | new: the eight panels. Interval schedule takes over `schedule.ui` and `hibernate.{h,cpp,ui}`, minus the row rebuilding. |
| `schedule`, `bittaglog`, `adxl362config`, `lsm6dsvconfig` (`.h`/`.cpp`), `configinterface.h`, `configfieldvisibility.h` | 1b | deleted |
| `pbenumgroup.{h,cpp}` | 1b | kept; the enum binding uses it |
| `host/applications/qtmon/main.cpp` | 0, 1b | `--config-probe` (0); `--check-config-panels` (1b) |
| `qtmon/tagconfiguration/README.md` | 1b | rewritten: panels, binding tables, the shape, the cross-check, the recipes |

`Adxl362Config` (363 lines of code and branching) becomes three panels. Each is
a `.ui` file and a few lines of `bind()`; the standard one also has its two
step slots.

## What this does not change

- **The default configs must still list every supported field, zeros
  included.** The cross-check now catches a mismatch with the panels, but not a
  mismatch with what the firmware actually does. Stage 2 addresses that.
- **The controls.** Each panel reproduces today's group boxes, labels, ranges
  and tooltips for its tag. Only their arrangement changes, from two tabs to
  one page, so the screenshots change once, deliberately.
- **The wire format and the firmware.**

## Test strategy

### The argument

The configuration screen is a function with no hardware in it: a `Config`
comes in (from the tag's `GetConfig`, a fixture, or a restored file), and a
`Config` goes out (what Start hands to `tag->Start()`). This proposal changes
nothing in the firmware or on the wire. So, for every input a tag can send and
every edit a user can make, if the new qtmonitor emits byte-for-byte the
`Config` the current one emits, no tag can tell the difference. That
equivalence can be checked on the host, against the current qtmonitor, for
every tag type at once.

What it cannot check is the USB attach and Start plumbing, which this proposal
does not touch. One live smoke test on any single tag covers that. No family
needs its own bench session.

### T1. `config_shape_check` (stage 1; tagcore, no Qt)

A standalone assertion program, the established pattern
(`host/libraries/tagcore/test/README.md`), built with `BUILD_TAGCORE_CHECKS`.
It asserts that:

- every embedded default config parses, and every key resolves to a `Config`
  field;
- for every tag type, `ConfigShape::forTag()` matches the generator's last
  output, with each `ConfigFieldVisibility` member mapped to its path. This is
  the gate for deleting the generator.
- `has()` of a valid but unsupported path is `false`, and of a non-field path
  fails loudly;
- `merge()` gives the outcomes in the restore table: it copies a stated zero,
  skips fields the file omits, skips fields the tag lacks, never copies
  `tag_type`, and limits `hibernate` to the tag's count.

### T2. A probe mode in qtmonitor

```sh
QT_QPA_PLATFORM=offscreen qtmonitor --config-probe cases/ --out results/
```

This mode runs a directory of case files through the real configuration screen
in one process, with no tag attached. It reuses the fixture mode's display-only
path. Each case names:

- an input `Config`;
- the tag state it is reported in;
- a fixed "now";
- optionally, a restore file;
- for today's code only, which tab is selected.

The probe calls `SetConfig()`, applies the restore if there is one, then
`GetConfig()`. For each case it writes:

- **`sent`**: the `GetConfig()` result in binary and JSON. This is exactly what
  Start would send.
- **`ui`**: a fingerprint of the screen. For every visible control it lists the
  group title, the label, the widget kind and current value, and:
  - for a spin box, its minimum, maximum, step, decimals and suffix;
  - for an enum group, every option's id and label, and which one is checked;
  - its tooltip.

  The fingerprint is sorted by group title and label, so it does not depend on
  layout. Two tabs becoming one page does not change it, and nothing else
  should. Numbers are printed in the C locale.

Two small, behaviour-preserving changes to today's code make the probe
deterministic. The schedule and hibernation code read the clock in seven
places, which become one `ConfigClock::now()` that the probe can set. The
fixture mode's "ignore Start" guard is reused as it stands.

### T3. A generated case corpus

`host/applications/qtmon/test/config_probe_cases.cc` writes the cases. It is a
small C++ program, built with `-DBUILD_QTMON_CHECKS=ON`, because it walks the
fields through protobuf reflection, and the host Python has no protobuf
module. The cases are committed, so it is needed only to change them.

- **Base inputs.** Inputs come from the configs captured from real tags in
  `host/docs/fixtures/qtmonitor/`, which is to say real firmware output:
  BitPresTag, BitTag LE, CompassTag, IMUTag and PresTag, plus BitTag, whose
  fixture is the default. Tags with no fixture use their default config:
  - BitTagNG's IDLE `readConfig()` decodes exactly that default, so it is real
    output too;
  - UIUCTag's `readConfig()` is hand-built, so its default is only a stand-in
    (see T5).
- **One field at a time,** for every path in the tag's shape:
  - an enum takes every defined value, `UNSPECIFIED`, and an undefined number;
  - a number takes 0, the widget's minimum and maximum, a value just outside
    each, and a value off the step;
  - the schedule tries a start in the past and in the future, an end set and
    indefinite, and hibernation rows set and unset.
- **Fields that depend on each other.** ADXL362 range is combined with a
  threshold above that range's maximum, and sample rate with an inactivity time
  off its step. This exercises the clamping that happens when one value
  narrows another's range, which depends on the order things are loaded and is
  exactly what a restructure gets wrong.
- **States.** Each base input is reported in IDLE and again in RUNNING, so the
  disabled display is fingerprinted too.
- **Restores.** Each tag type gets a partial file, a full file for that tag, a
  full file for another tag, a file with explicit zeros, and a malformed file.

That is a few hundred cases, and the probe runs them in seconds.

### T4. Differential comparison against today's qtmonitor

The probe is added to today's code first (rollout step 0). A qtmonitor built
from that commit is the **baseline**. Its outputs over the corpus are committed
as golden files under `host/applications/qtmon/test/golden/`, so later stages,
and later changes, compare against them without rebuilding the baseline.

```sh
host/applications/qtmon/test/compare.py <baseline-results> <new-results>
```

The script diffs `sent` byte for byte, and `ui` line by line. **Every
difference must be listed in `expected-differences.md` with its reason**, or the
stage is not done. The differences the proposal expects:

| Stage | Expected differences |
| --- | --- |
| `isVisible()` fix | In the baseline, a case's `sent` may depend on the selected tab. After the fix it must not. The goldens are regenerated once, with the list of changed cases. |
| 1 | Restores only. A file's explicit zeros are now copied. Fields outside the tag's shape are no longer copied from another tag's file: today's merge copies whole sub-messages such as `adxl362`. No other `sent` or `ui` change. |
| 1b | None in `sent` or `ui`. The tab selection disappears from the cases. |

User edits are covered by the corpus without driving widgets, because widget
identities change between old and new and could not be compared directly.
Loading a value and storing it is the same path a user's edit takes once the
widget holds that value, and the dependent-range combinations exercise the
interactions. The one behaviour this misses is a range narrowing *while the
user types*. A few direct widget checks on the new ADXL362 panel cover it:
change the range, assert the threshold's maximum and step.

### T5. Firmware acceptance on the host (optional, recommended)

The only tag-specific code on the configuration path is each family's
`config.c`. As the UIUCTag simulations already do
(`embedded/tags/UIUCTag/test/README.md`), it can be compiled for the host
against stubs for `pState`, the stored-config flash write and the clock. A
`config_accept_sim` would then:

- take every `sent` config from T4;
- pass it to the real `writeConfig()`, and assert it is accepted (or rejected
  exactly where the firmware means to reject, such as PresTag with period 0);
- read it back through the real `readConfig()` in the configured state, and
  assert the values round-trip under the firmware's own rules
  (`start_delay` + 1, the fixed ADXL ranges);
- report the real IDLE `readConfig()` output for the hand-built families, to
  replace the default-config stand-ins in T3.

T4 alone proves that the restructure changes nothing, since the firmware does
not change. T5 adds a different guarantee: that what qtmonitor sends is what
each firmware accepts. It also catches a default config that disagrees with its
firmware, which is the one gap the cross-check cannot see. The stubs are shared
across families, and each family adds one `config.c`. Start with UIUCTag, which
has neither a captured fixture nor a default-decoding `readConfig()`, and has
the stub tree already.

### T6. The panel cross-check (stage 1b)

`qtmonitor --check-config-panels`, described above, is run with the probe.

### T7. One live smoke test

On whichever tag is on the bench: attach, Read, Save, Restore, Start, then let
it reach CONFIGURED or RUNNING. Stop it, and read the stored configuration back
with `tag-info`. Do not re-attach while it runs. Nothing is flashed, so the
flashing protocol does not apply. This exercises the plumbing, which no host
test can. It is not a per-family test: per-family behaviour is T4's job, and
T5's.

### Screenshots

The documentation screenshots are rebuilt after stage 1b, and change once
because the page layout changes. They are a visual check, not the test. The
fingerprint is the precise one.

## Rollout

0. **Test scaffolding, on today's code** (done 2026-10-23): `ConfigClock`,
   `--config-probe`, the fingerprint, the case generator (686 cases), the
   goldens. These change no behaviour. Two probe runs give identical output,
   and a deliberate label change and `start_delay` change both showed up in
   every IMUTag case.
1. **`isVisible()` fix** (done 2026-10-23). The probe confirmed the defect: in
   69 cases, `sent` differed between tabs. The four `isVisible()` tests are now
   `!isHidden()`. Exactly those 69 cases changed, each to the configuration the
   Sensors tab already sent.
2. **Stage 1:** `ConfigShape`, the merge in qtmonitor and `tag-start`, the
   adapter, T1. Delete the generator. T4 shows only the restore-zero
   differences.
3. **Stage 1b:** the panels on one page, `kPanelsForTag`, T6. Delete the
   modules, the adapter and `ConfigFieldVisibility`. T4 shows no differences.
   Rebuild the screenshots. Run T7 once.
4. **T5,** whenever convenient. It does not gate the restructure.

## Open questions

- **The Python `tagclient` example** uses `MergeFrom`. Should it mirror
  `merge()`?
- **`Adxl362.data_format`, `channels` and `active_sec`** have visibility bits
  today but no default config and no control. They need nothing until a tag
  lists them, and then the cross-check will demand a panel.
- **Panel order on the single page.** The proposal stacks the panels as
  schedule, then data, then sensors. A different order is a one-line change.

## Stage 2 (deferred): the tag reports its own shape

Stages 1 and 1b take a tag's shape from its default config, not from the tag.
If that ever matters, for example because a firmware/JSON mismatch causes
trouble or because tags of one type start to differ, the tag can report its
shape itself. Panel selection stays by tag type. The tag's reported shape
replaces the default config as the input to the merge, and can be checked
against the selected panels at attach time.

**Mechanism.** Mark the scalar and enum fields of `Config`, `Adxl362` and
`Lsm6dsv` as proto3 `optional`, and add `uint32 config_schema = 10`. Firmware
that sets `config_schema = 1` promises that every field it supports is present
(`has_x`), zeros included. The vendored nanopb 0.4.9.1 supports this:
`TagInfo.ppm_clock_error` already generates a `has_` flag, and `pb_encode.c`
writes an optional field whenever that flag is set.

**Why old tags keep working.** `optional` changes no bytes on the wire. It
changes whether the sender writes a zero, and whether the receiver keeps a
presence flag. An old tag, built from today's `.proto`, keeps omitting zeros,
and every nonzero value it sends is byte-for-byte what it sends now. On its
config, `has_x()` means "nonzero", not "supported". So the host uses the tag's
presence only when `config_schema >= 1`, and otherwise falls back to
`ConfigShape::forTag()`.

| | Old firmware | New firmware |
| --- | --- | --- |
| Old host | today | The old host uses its own table. Zeros it sends arrive without presence. |
| New host | shape from default config | shape from the tag |

**Firmware rules for adopters:**

- `readConfig()` sets the marker and every supported field's `has_` flag.
- `writeConfig()` treats an absent field as zero, never as "keep" or "reject",
  because older hosts never send zeros.
- Unsupported fields are still ignored on write.

Each family would adopt the marker at its own release, or never.

**Costs that kept it out of stage 1.**

- Every nanopb output is regenerated, and each `Config` struct grows, so every
  target needs a `.list` check.
- The firmware work is per family, under the flashing protocol.
- Protobuf JSON output stops printing absent `optional` fields. That affects
  `tag-info` records, and the Python readers of those have not been audited.
