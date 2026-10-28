---
type: proposal
status: proposed
summary: Split sensorviz into a Qt-free core (Session, StreamCatalog, ViewModel, TransformRegistry, PreferenceStore over sensordata) and thin front ends, so the Qt app and a possible webview app are both clients of one implementation.
---

# A Toolkit-Neutral Core for sensorviz

**Status:** phase A is built (`13516ce2`): `sensorviz_core` with a Qt-free
`ViewModel`, `PlotSpec`/`buildPlotSpec()` and `StreamMeta`, and menus that show
the state instead of holding it. [Phase A as built](#phase-a-as-built) records
where it differs from the sketch below. Phase B is built: `openLog()` and
`StreamCatalog` in the core, the Qt loader removed;
[phase B as built](#phase-b-as-built) records its differences. Phase C is
built: `PreferenceStore` reads and writes the preference file in the core
([phase C as built](#phase-c-as-built)). Phases C2, D+E and F are not built; see [Phases](#phases) for the revised order.

sensorviz should be split into a **Qt-free core** and **thin front ends**.

- **The core** holds every decision about what the plot shows and what the
  data means:
  - loading and the stream catalog;
  - view state;
  - transforms and their parameters;
  - preferences;
  - plot policy;
  - hover and time formatting.
- **The Qt app** keeps only widgets, menus, dialogs and the QCustomPlot
  renderer.
- **A webview app** (Tauri, Saucer or similar) would be a second client of the
  same core, not a rewrite.

Each concern has one owning C++ class with a narrow interface. CMake enforces
the line by never linking Qt to the core.

The [decimating backend](decimating-backend.md) already did the hardest part.
Sample data, views, hover values, ranges and gaps live in the Qt-free
`sensordata` library, and QCustomPlot is confined to `PlotView`. What remains
is the logic still in `MainWindow` and in Qt-typed structs.

## Where the Qt dependency is today

At `6e0134e4`, 6,182 lines in `host/applications/sensorviz`:

| Concern | Where | Qt dependency |
| --- | --- | --- |
| Samples, views, gaps | `sensordata` | none |
| Plot policy | `buildPlotSpec()` in `plot_spec.cpp` | `QString`, `QColor`, `QVector`, `QMap` in its types; the logic is pure |
| Rendering | `PlotView` | QCustomPlot; that is its job |
| View state | `MainWindow` members and `QAction` check states | `custom_axis_ranges_` is a `QMap<QString, QCPRange>`, a QCustomPlot type outside the renderer |
| Loading, stream catalog, info, events, calibration | `sqlite_loader.cpp`, `applyLoadedLog()` | `QString`, `QMap`, `QJsonDocument` |
| Transforms (magnitudes, altitude, activity filter, compass) | `MainWindow` methods in `transforms.cpp`, `compass_transforms.cpp` | `QAction`, `QInputDialog`, `QQuaternion`, sensoranalysis's Qt adapter |
| Heading, pitch, roll | `compassAnglesFromQuaternion()` (sensorui) | Qt's `QQuaternion::toEulerAngles()`, by [decision 0030](../../../../../docs/decisions/0030-host-orientation-stored-as-quaternion-only.md) |
| Preferences | `sensor_preferences.cpp` | `QJsonDocument`, `QFile`, file dialogs |
| Hover readout, time labels | `streamValueAt()`, `formatPlotTime()` in `interaction.cpp` | `QString`, `QDateTime`, `QTimeZone` |

About 1,900 lines of domain logic sit in Qt-dependent files. That is
`transforms.cpp`, `compass_transforms.cpp`, `sensor_preferences.cpp`, most of
`sqlite_loader.cpp`, and the formatting in `interaction.cpp`.

## Class boundaries

```text
                    ┌──────────────── front ends ────────────────┐
                    │  Qt: MainWindow, PlotView, dialogs          │
                    │  webview: commands + uPlot page (possible)  │
                    │  session_cli: JSON dump (check program)     │
                    └──────────────────────┬─────────────────────┘
                                           │  Session API only
┌──────────────────────── sensorviz_core (Qt-free) ─────────────────────────┐
│ Session ── owns ──┬─ sensordata::Log        samples, view(), valueAt()     │
│  (one per log)    ├─ StreamCatalog          what the log and transforms    │
│                   │                         contain: streams, record sets, │
│                   │                         info, events, calibration      │
│                   ├─ TransformRegistry ──── Transform (one per transform)  │
│                   └─ ViewModel              what the user chose; plotSpec()│
│         uses ───── PreferenceStore          per-tag sparse overrides, JSON;│
│                    (app lifetime)           outlives any one Session       │
│ free functions: buildPlotSpec(), defaultDisplayForStream(), formatTime()   │
└────────────────────────────────────────────────────────────────────────────┘
        sensordata (Qt-free)        sensoranalysis_core (Qt-free)
```

Rules:

- The core includes no Qt header and links no Qt library. `sensorviz_core` and
  its check programs build with `BUILD_QT_APPS=OFF`, so the Linux and Windows
  build checks catch a stray Qt include.
- Core types use `std::string` (UTF-8), `std::vector`, `std::map`,
  `std::optional` and a small `Rgb` struct. Conversion to Qt types happens
  only in the Qt layer.
- Front ends call only `Session`. They may read `sensordata::Envelope` arrays
  that `Session` hands them, but they never mutate the log, catalog or state
  directly.

### `sensordata::Log` (exists)

`sensordata::Log` holds the samples and answers `view()`, `valueAt()` and
`range()`. It also finds gaps and accepts `addDerived()`/`addComputed()`.
No change is needed. See the
[sensordata README](../../../../libraries/sensordata/README.md).

### `StreamCatalog`

`StreamCatalog` describes the log for everything above `sensordata`:

- the stream and record-set metadata from the `streams` table;
- the tag type and profile;
- the `info` table;
- event markers;
- the newest calibration;
- the derived streams that transforms have registered.

**What it replaces.** It replaces the Qt-typed `SensorLog`, `SensorStream` and
`SensorRecordSet`, and the metadata half of `sqlite_loader.cpp`.

**How it is filled.** A free function, `openLog(path, progress)`, returns
`{std::unique_ptr<sensordata::Log>, StreamCatalog}`. Calibration JSON goes
through `sensoranalysis_core_json`, which `dataprocessing` already uses, so the
numbers are read exactly as today.

Reading the `info` table and events could move into `sensordata` instead. Keep
them in the catalog unless another tool needs them.

### `ViewModel` (first slice, in progress)

`ViewModel` holds what the user chose:

- visible streams and their order;
- custom and explicit ranges;
- colors and axis sides;
- title and its visibility;
- UTC offset;
- cursors and break-at-gaps;
- the current transform parameter values.

`plotSpec()` calls `buildPlotSpec()` with the catalog and these choices. Every
setter reports what changed. Menus and dialogs read and write the
`ViewModel`; they do not store state themselves.

### `Transform` and `TransformRegistry`

The four transforms become classes, written once and declared to the
registry:

```cpp
struct ParameterSpec {
    std::string id, label, units;
    enum class Kind { Number, Toggle } kind;
    double minimum, maximum, step, defaultValue;  // Number only
};

class Transform {
public:
    virtual ~Transform() = default;
    virtual std::string id() const = 0;
    virtual bool applicable(const StreamCatalog &catalog) const = 0;  // inputs present?
    virtual std::vector<ParameterSpec> parameters() const = 0;
    virtual std::vector<std::string> outputs() const = 0;              // stream ids
    virtual bool run(TransformContext &context, const ParameterValues &values,
                     std::string &error) = 0;
};
```

`TransformContext` gives a transform three things:

- read access to the catalog;
- the `sensordata::Log` input paths: `forEachRow()` `double`s for anything
  that goes through sensoranalysis, which keeps the bit-identical rule, and
  the in-memory `float` columns for display transforms such as the IMU
  magnitudes;
- registration of outputs: `addDerived()` or `addComputed()`, plus a catalog
  entry with display defaults.

The registry has four jobs:

- run every applicable transform after load;
- re-run only the affected transform when a parameter changes;
- remove its outputs when it stops being applicable;
- list the parameters so a front end can build its own controls.

**Parameters are data, not dialog factories.** The TODO's "configuration UI
factory" becomes `parameters()`. The Qt app renders a `ParameterSpec` with one
generic dialog, replacing the four action-specific dialogs in
`transforms.cpp` and `compass_transforms.cpp`; a webview renders it as a form.

Rules that carry over:

- IMUTag orientation is never computed by pairing `ImuAccel` with `ImuMag`.
  The compass transform is applicable to a CompassTag `compass_raw` record set,
  or to a stored `compass_orientation` or `imu_compass_orientation` record set
  from `dataprocessing`, and to nothing else.
- Settings-dependent results are never written to the log. Transforms
  register in-memory derived streams only.

### `PreferenceStore`

`PreferenceStore` holds per-tag sparse overrides: visibility, colors, axis
sides and break-at-gaps. It loads and saves them in today's JSON format, byte
for byte, and the `ViewModel` applies them.

JSON goes through protobuf's `JsonStringToMessage`/`MessageToJsonString` on
`google.protobuf.Struct`. The host already links protobuf, and
`sensoranalysis_core_json` uses the same route, so no dependency is added.
Choosing a file stays in the front end.

### `Session`

`Session` is the facade the front ends see. There is one per open log. It
owns the log, catalog, registry and view model, and uses a `PreferenceStore`
that the front end owns for the life of the app. That matches today, where
preferences for several tag types survive from one log to the next. It
offers:

| Call | Purpose |
| --- | --- |
| `Session::open(path, preferences, progress)` | Build a new session (load, run transforms, apply this tag type's preferences) |
| `catalog()`, `viewModel()` (const) | What exists, what is chosen: for building menus |
| `set…()` (forwarded to `ViewModel`) and `setParameter(transform, id, value)` | Every user change; each returns a `Changes` set |
| `plotSpec()` | The current spec |
| `envelope(trace, t0, t1, columns)` | `sensordata::Envelope` for one trace |
| `hover(t)` | Rows of `{label, text}`: today's `streamValueAt()`, formatted in the core |
| `formatTime(t)` | Axis and readout time text: epoch with UTC offset, or elapsed |
| `loadPreferences(bytes)`, `savePreferences()` | Bytes in and out; file choice stays in the front end |

**Change reporting.** `Changes` is a small bit set: `Spec`, `Catalog`,
`Parameters`, `Preferences`. The Qt app maps it to its refreshes, and a webview
command returns it to the page. No observer or signal mechanism is needed.

**Threading.**

- `Session::open()` builds a new session, so it can run on a worker thread
  that hands the result to the UI thread.
- After that, all mutating calls happen on one thread.
- `envelope()` and `hover()` are const and may be called concurrently,
  inheriting `sensordata`'s guarantee.

### Front ends

- **Qt** (the shipped app). `MainWindow` builds menus from `catalog()` and
  `viewModel()` and keeps them in step with one sync function. Every action
  and dialog calls a `Session` setter and applies the returned `Changes`.
  `PlotView` draws a `PlotSpec` from `envelope()` arrays. The compass panel,
  printing and documentation capture stay Qt-specific.
- **`session_cli`** (a check program, not shipped).
  - It opens a log, applies a script of setter calls, and prints the
    `PlotSpec` as JSON plus envelope sizes and hover rows.
  - As a third client that is neither Qt nor a webview, it shows the boundary
    is real.
  - It gives the screenshot suites a Qt-free counterpart for regression
    checks.
- **Webview** (possible). Each `Session` call becomes one shell command.
  - `plotSpec()` and `hover()` travel as JSON.
  - `envelope()` travels as raw binary, a `Float64Array` of times and a
    `Float32Array` of values, about 0.4 MB for a six-stream full redraw.
  - uPlot draws from `PlotSpec` traces and axes.
  - The shell itself (Tauri over a small C++ facade, or Saucer or
    webview/webview directly over `Session`) is a separate decision. This
    split does not depend on it.

## Interface sketch

This sets out the shape of the interfaces, not compilable code. Names follow
the existing `sensordata` API (`StreamId`, `Envelope`, `Sample`, `GapMode`).
Everything is in namespace `sensorviz` unless qualified. Strings are UTF-8.

### Common types

```cpp
struct Rgb { uint8_t r, g, b; };
enum class AxisSide { Left, Right };
enum class TimeDomain { Epoch, Elapsed };        // mirrors sensordata::TimeDomain
struct Range { double lower, upper; };

enum class Change : uint32_t {                   // what a mutation invalidated
    None = 0, Spec = 1, Catalog = 2, Parameters = 4, Preferences = 8,
};
using Changes = Flags<Change>;                   // bit set; Catalog implies Spec

using ParameterValue  = std::variant<double, bool>;
using ParameterValues = std::map<std::string, ParameterValue>;  // key "transform/param",
                                                                // e.g. "altitude/sea_level_mbar"
```

### StreamCatalog

```cpp
struct StreamEntry {
    std::string id;                  // "imu_ax", "altitude"; unique in the catalog
    std::string label, units;
    std::string group;               // family for linked y ranges ("imu_accel"), or ""
    TimeDomain domain;
    sensordata::StreamId data;       // its samples in the Log; kNoStream if none
    enum class Origin { Stored, Derived } origin;
    std::string producer;            // transform id when Derived, else ""
};

struct RecordSetEntry {              // multi-column sets: compass_raw, *_orientation
    std::string id, label, table;
    TimeDomain domain;
    std::vector<std::string> columns;
    sensordata::StreamId timeSource;
};

struct EventMarkers { std::string id, label; TimeDomain domain; std::vector<double> times; };

struct CalibrationInfo {
    bool present = false;
    int64_t epoch = 0;
    compasscore::CalibrationConstants constants;
    std::string warning;             // non-empty when present but suspect
};

class StreamCatalog {
public:
    const std::string &path() const;
    const std::string &tagType() const;
    const std::string &preferenceKey() const;    // today's preferenceKey(): tag type/profile
    TimeDomain timeDomain() const;
    std::optional<int64_t> collectionStartEpochMs() const;
    const std::vector<std::pair<std::string, std::string>> &info() const;  // File Info rows

    const std::vector<StreamEntry> &streams() const;   // stored in load order, then derived
    const StreamEntry *stream(std::string_view id) const;
    const RecordSetEntry *recordSet(std::string_view id) const;
    const std::vector<EventMarkers> &events() const;
    const CalibrationInfo &calibration() const;

private:
    friend class TransformContext;   // the only writer after open: adds and removes Derived entries
};

struct OpenedLog { std::unique_ptr<sensordata::Log> log; StreamCatalog catalog; };
std::optional<OpenedLog> openLog(const std::string &path,
                                 const std::function<void(double fraction)> &progress,
                                 std::string &error);
```

Invariants:

- Every `StreamEntry` with `data != kNoStream` refers to a live stream in the
  same `Log`.
- Stored entries never change after `openLog()`.
- Derived entries are owned by exactly one transform, their `producer`.

### Display defaults and PlotSpec

```cpp
struct DisplayDefaults { Rgb color; bool visible; AxisSide side; std::optional<Range> fixedRange; };
DisplayDefaults defaultDisplayForStream(std::string_view streamId);   // today's table, unchanged

enum class AxisRangeMode { Auto, Linked, Fixed };
struct PlotAxis {
    AxisSide side; std::string label; Rgb color;
    AxisRangeMode mode; std::string linkedGroup;
    std::optional<Range> range;      // absent: renderer pads its current range
};
struct PlotTrace {
    std::string id, name;
    sensordata::StreamId data;
    Rgb color; int penWidth; int axis;   // index into PlotSpec::axes
};
struct PlotSpec {
    TimeDomain domain; int utcOffsetHours;
    std::optional<Range> xDataRange;     // full-range zoom; absent when nothing has samples
    std::vector<PlotTrace> traces;       // drawing order
    std::vector<PlotAxis> axes;          // one per trace
    std::vector<double> eventMarkers;
    std::vector<std::string> metadataRows;
    std::string title;                   // empty: no title
    bool breakAtGaps;
};

PlotSpec buildPlotSpec(const StreamCatalog &, const sensordata::Log &,
                       const ViewState &, const ParameterValues &);   // pure
std::string toJson(const PlotSpec &);                                 // session_cli, webview
```

### ViewModel

```cpp
struct ViewState {                       // plain, copyable, comparable: what tests diff
    std::vector<std::string> visible;    // drawing and axis order
    std::map<std::string, Range> customRanges;
    std::set<std::string> explicitRanges;        // ranges the user set, not linked ones
    std::map<std::string, Rgb> colors;           // overrides only
    std::map<std::string, AxisSide> axisSides;   // overrides only
    std::string title; bool titleVisible = true;
    int utcOffsetHours = 0;
    bool cursorsVisible = true;
    bool breakAtGaps = true;
    ParameterValues parameters;                  // current transform parameter values
};

class ViewModel {
public:
    explicit ViewModel(const StreamCatalog &catalog);    // catalog outlives the model
    const ViewState &state() const;

    // Effective values: override, else display default.
    bool isVisible(std::string_view id) const;
    Rgb color(std::string_view id) const;
    AxisSide axisSide(std::string_view id) const;
    std::optional<Range> range(std::string_view id) const;

    Changes setVisible(std::string_view id, bool visible);
    Changes setVisibleSet(std::vector<std::string> idsInOrder);
    Changes setColor(std::string_view id, std::optional<Rgb>);        // nullopt: back to default
    Changes setAxisSide(std::string_view id, std::optional<AxisSide>);
    Changes setRange(std::string_view id, std::optional<Range>);
    Changes setTitle(std::string); Changes setTitleVisible(bool);
    Changes setUtcOffset(int hours); Changes setCursorsVisible(bool); Changes setBreakAtGaps(bool);
    Changes setParameter(std::string_view key, ParameterValue);       // returns Parameters | Spec

    Changes applyPreferences(const TagPreferences &);
    Changes catalogChanged();    // drop ids no longer in the catalog; new derived ids take
                                 // their default visibility; keeps order otherwise

    PlotSpec plotSpec(const sensordata::Log &) const;
};
```

Invariants:

- `visible` holds only ids present in the catalog, with no duplicates.
- Overrides equal to the default are not stored, which keeps preferences
  sparse.
- A setter that changes nothing returns `Change::None`.

### Transforms

```cpp
struct ParameterSpec {
    std::string key;                 // "altitude/sea_level_mbar"
    std::string label, units;
    enum class Kind { Number, Toggle } kind;
    double minimum, maximum, step;   // Number only
    ParameterValue defaultValue;
};

class TransformContext {             // what a running transform may touch
public:
    const StreamCatalog &catalog() const;
    const sensordata::Log &log() const;

    // Inputs. Anything passed to sensoranalysis reads doubles from SQLite;
    // display-only transforms may read the Log's float columns.
    bool forEachRow(const std::string &table, const std::vector<std::string> &columns,
                    const std::function<void(double time, const double *values)> &,
                    std::string &error) const;

    // Outputs, tagged with the running transform's id.
    std::optional<sensordata::StreamId> addDerived(StreamEntry meta, sensordata::StreamId timeFrom,
                                                   std::vector<double> values);
    std::optional<sensordata::StreamId> addDerived(StreamEntry meta, std::vector<double> time,
                                                   std::vector<double> values);
    std::optional<sensordata::StreamId> addComputed(StreamEntry meta,
                                                    std::vector<sensordata::StreamId> inputs,
                                                    ComputeFn);
};

class Transform {
public:
    virtual ~Transform() = default;
    virtual std::string id() const = 0;                        // "altitude"
    virtual bool applicable(const StreamCatalog &) const = 0;  // inputs present
    virtual std::vector<ParameterSpec> parameters() const = 0;
    virtual std::vector<std::string> outputs(const StreamCatalog &) const = 0;
    virtual bool run(TransformContext &, const ParameterValues &, std::string &error) = 0;
};

class TransformRegistry {
public:
    static TransformRegistry makeDefault();   // imu-magnitudes, altitude, activity-filter, compass
    std::vector<ParameterSpec> parameters(const StreamCatalog &) const;   // applicable ones only
    bool runAll(TransformContext &, const ParameterValues &, std::vector<std::string> &errors);
    // Removes the transform's previous outputs (Log and catalog), then runs it again.
    bool rerun(std::string_view transformId, TransformContext &, const ParameterValues &,
               std::string &error);
    std::string ownerOf(std::string_view parameterKey) const;
};
```

Invariants:

- A transform reads only stored streams, never another transform's output.
  That is true today, so the registry needs no dependency ordering.
  `TransformContext` enforces it by refusing derived inputs.
- Compass applicability is `compass_raw`, or a stored `compass_orientation`
  or `imu_compass_orientation` record set, and nothing else.
- A failed transform leaves no outputs behind and reports its error. Other
  transforms still run.

### PreferenceStore

```cpp
struct TagPreferences {                          // sparse overrides for one preference key
    std::optional<std::set<std::string>> visible;
    std::map<std::string, Rgb> colors;
    std::map<std::string, AxisSide> axisSides;
    std::optional<bool> breakAtGaps;
};

class PreferenceStore {                          // app lifetime; outlives Sessions
public:
    bool load(std::string_view json, std::string &error);   // replaces everything
    std::string save() const;                    // today's formatted JSON, byte-compatible
    const TagPreferences *forKey(std::string_view preferenceKey) const;
    void capture(std::string_view preferenceKey, const ViewModel &);   // differences from defaults only
    void clear(std::string_view preferenceKey);
};
```

### Session

```cpp
struct HoverRow { std::string traceId, label, text; };   // text empty inside a gap
enum class TimeStyle { AxisTick, Readout };

class Session {
public:
    static std::unique_ptr<Session> open(const std::string &path, PreferenceStore &preferences,
                                         const std::function<void(double)> &progress,
                                         std::string &error);   // worker thread is fine

    const StreamCatalog &catalog() const;
    const ViewModel &view() const;
    std::vector<ParameterSpec> parameters() const;
    std::vector<std::string> transformErrors() const;   // from the last run

    // Mutations: one thread. Each forwards to ViewModel; setParameter also reruns
    // the owning transform and calls ViewModel::catalogChanged().
    Changes setVisible(std::string_view id, bool);
    // ... one forwarder per ViewModel setter ...
    Changes setParameter(std::string_view key, ParameterValue);
    Changes rememberPreferences();                 // capture into the store
    Changes resetPreferences();                    // clear this key, reapply defaults
    Changes reapplyPreferences();                  // after the front end loaded a file into the store

    // Reads: const, safe concurrently with each other.
    PlotSpec plotSpec() const;
    sensordata::Envelope envelope(const PlotTrace &, double t0, double t1, int columns) const;
    std::vector<HoverRow> hover(double t) const;
    std::string formatTime(double t, TimeStyle) const;
};
```

### Qt adapter

```cpp
class MainWindow : public QMainWindow {
    PreferenceStore preferences_;                // app lifetime
    std::unique_ptr<Session> session_;
    PlotView *plot_;
    void apply(Changes);         // the only refresh path: Catalog rebuilds menus,
                                 // Spec calls plot_->setSpec(session_->plotSpec()),
                                 // always syncActions()
    void syncActions();          // checked/visible/enabled from session_->view()
};

class PlotView : public QWidget {                // unchanged role: only QCustomPlot code
public:
    void setSession(const Session *);            // for envelope() and hover()
    void setSpec(const PlotSpec &);
signals:
    void hoverTime(double t); void xRangeChanged(double, double);
    void cursorsChanged(double, double); void contextMenuRequested(QPoint);
};

namespace ParameterDialog {                      // one dialog for every ParameterSpec
bool edit(QWidget *parent, const ParameterSpec &, ParameterValue &inOut);
}
```

### Webview commands

Each command is a `Session` call. State and specs travel as JSON, envelopes
as binary.

| Command | Session call | Payload back |
| --- | --- | --- |
| `open(path)` | `Session::open` | catalog JSON, `Changes` |
| `set(name, args)` | the matching setter | `Changes` |
| `spec()` | `plotSpec()` | `PlotSpec` JSON |
| `envelope(trace, t0, t1, cols)` | `envelope()` | `Float64Array` time, `Float32Array` value |
| `hover(t)` | `hover()` | rows JSON |
| `preferences.load(bytes)` / `.save()` | store `load`/`save` + `reapplyPreferences()` | `Changes` / bytes |

### Phase A as built

Phase A (`13516ce2`) follows this sketch with four differences. Each is kept
or folded into a later phase as follows.

| As built | Sketch | Resolution |
| --- | --- | --- |
| `ViewModel` holds the plot's stream list (`StreamMeta`; `resetForLog()`, `addOrReplaceStream()`, `removeStream()`) | `StreamCatalog` owns streams; `ViewModel` refers to it | Phase B moves the list into `StreamCatalog`; `ViewModel` keeps only choices about streams |
| Fine-grained changes: `Streams`, `Visibility`, `Ranges`, `Appearance`, `Title`, `UtcOffset`, `Cursors`, `Gaps`, `Parameters` | `Spec`, `Catalog`, `Parameters`, `Preferences` | Keep the fine-grained set; `Streams` is the sketch's `Catalog`, and every flag implies `Spec` |
| Setters return their changes *and* notify an optional listener | Returned changes only | Keep both; the listener suits Qt, the return value suits commands |
| Typed parameter setters (`setSeaLevelPressure()`, `setDeclinationDegrees()`, `setBatteryForward()`, `setActivityTimeConstant()`) | `setParameter(key, value)` over `ParameterValues` | Phase D replaces them when transforms declare their parameters |

`TagPreferences` is in the core; its file I/O followed in phase C. The
sensorviz TODO lists everything still Qt-bound.

### Phase B as built

`sensorviz::openLog()` and `StreamCatalog` (`core/stream_catalog.h`) replaced
`sqlite_loader.cpp`; `SensorLog`, `SensorRecordSet` and `SensorEventMarker`
are now aliases of the core's `StreamCatalog`, `RecordSet` and `EventMarker`,
and the Qt app converts strings where it shows them. Calibration is read by
`compasscore::calibrationFromJson()` (`sensoranalysis_core_json`) and the
IMUTag magnetometer corrected by `compasscore::applyMagnetometer()`.

| As built | Sketch | Resolution |
| --- | --- | --- |
| A plain struct with the old `SensorLog` field names, holding the `std::shared_ptr<sensordata::Log>` | A class with accessors; `openLog()` returns `{unique_ptr<Log>, StreamCatalog}` | Kept for now: the Qt app copies the result between the loader thread and the window, and the field names kept the change to the call sites mechanical. Phase F's `Session` owns both and can hide the fields |
| `openLog(path, catalog, error)` returning `bool` | `optional<OpenedLog>` with a progress callback | Progress is not wired in the Qt app today; add it with `Session` |
| `ViewModel` still holds the stream list, stored and derived | The catalog owns streams | Moves with phase D: the list changes only when transforms add or remove derived streams, which the registry will own |
| No `Origin`/`producer` on streams | `StreamEntry::origin`, `producer` | Phase D, with the registry |

**Checked:**

- **Old and new loaders on every log.** A one-off program ran the old Qt
  loader and `openLog()` side by side over every log on hand: 54 files, five
  tag types (BitTag LE, CompassTag, IMUTag, PresTag and UIUCTag), 31 of them
  with calibration tables, and three that both refused. It compared every
  catalog field, every stream's metadata, every stream's envelope at 1,000
  columns and at one column per sample (2,605,928 points, bit for bit), and
  exact read-back at 201 times per stream. Everything was identical.
- **Capture suites.** The startup, `imutag`, `compasstag` and `menus` suites
  (13 images, including both File Info tabs) were pixel-identical before and
  after.
- **Core checks.** `plotspec_check` and `viewmodel_check` were unchanged.
  `catalog_check` is new; mutating the accelerometer correction or the resync
  match makes it fail.

**Behaviour differences, none reachable from a log tagcore writes:**

- Text that is not JSON now gives the warning "Compass calibration constants
  are not a valid JSON object: ...", with protobuf's message, instead of Qt's
  wording.
- A calibration whose `magnetometer` value is an empty object `{}` loads as
  the identity calibration; Qt rejected it as missing.
- Tag-type, table and event names are matched ignoring ASCII case, not
  Unicode case.
- `info` rows are ordered by UTF-8 bytes, not UTF-16 code units. The two
  orders differ only between characters above U+FFFF and those from U+E000
  to U+FFFF.

### Phase C as built

`sensorviz::PreferenceStore` (`core/preference_store.h`) holds the per-tag
`TagPreferences` and reads and writes the preference file;
`sensor_preferences.cpp` keeps only the menu, the file dialogs and the file
I/O. Colors are parsed by the core's `colorFromString()`, which follows
`QColor(QString)`.

| As built | Sketch | Resolution |
| --- | --- | --- |
| Reading through protobuf's `google.protobuf.Value`; writing by hand | `MessageToJsonString` for writing too | protobuf's printer cannot produce Qt's layout (four-space indents, `"key": value`, one array element per line), so the writer is about sixty lines |
| Keys ordered by UTF-16 code unit | -- | Qt orders object keys so, which puts characters above U+FFFF before U+E000-U+FFFF; the writer sorts the same way, and the reader reports the first fault in that order |
| The SVG color table copied from `QColor::colorNames()` | -- | Needed to read hand-edited names without Qt; 12- and 16-bit hex channels are rounded through 16 bits as Qt rounds them |

**Checked:** a one-off program ran the Qt code, copied verbatim, against the
store. Over 3,000 random preference sets (with quotes, backslashes, control
characters, U+2028, U+E000 and characters above U+FFFF in tags and ids) the
saved bytes were identical; over 63,000 loads of those files and random
mutations of them, the result and error message were identical whenever both
readers parsed the JSON; and `colorFromString()` agreed with `QColor` on
about 300,000 strings, including every color name in four spellings. The
capture suites were pixel-identical, and a preference file loaded through
`--load-preferences` drew as expected. `preference_check` is new.

**Malformed JSON** is the one difference: protobuf's reader accepts trailing
commas, which Qt refused, and refuses duplicate keys, unknown escapes such as
`\,` and numbers such as `1.`, which Qt accepted. None comes from a file
sensorViz writes; matching them would mean writing a JSON parser that copies
Qt's.

## The compass convention needs a decision

Heading, pitch and roll are plotted streams, separate from the compass
visualizer. Decision 0030 defined their convention as Qt's `toEulerAngles()`
applied where angles are shown, and rejected porting it as unnecessary
"because every display is a Qt display". A Qt-free core removes that
premise.

| Option | Effect |
| --- | --- |
| **Copy Qt's `QQuaternion::toEulerAngles()` into `sensormath`**, gimbal-lock branch included, as `vecmath.h` already copies `QVector3D`/`QQuaternion` arithmetic | Every displayed angle unchanged; a parity check against Qt joins the existing `compass_core_parity_check` |
| Use the explicit formulas in the `dataprocessing` design | Agrees with Qt to 3e-4 degrees, differs near ±90 degrees pitch |
| Adopt a standard Z-Y-X convention | Changes tilted headings everywhere; 0030 already rejected it for that reason |

Recommendation: the first option. A new decision record supersedes only the
"Qt's function at each display" part of 0030; storing only the quaternion
stands. The compass transform then uses `sensoranalysis_core`, which is
already bit-identical to the Qt adapter, instead of `CompassProcessor`.

## Phases

Each phase leaves the Qt app shipping. Each is checked the way the decimating
backend phases were:

- capture suites (`imutag`, `compasstag`, menus) pixel-identical;
- hover readouts identical;
- preference files byte-identical;
- full-log memory and load time re-measured into the
  [results log](decimating-backend-results.md).

| Phase | Work |
| --- | --- |
| A | `sensorviz_core` target; Qt-free `PlotSpec`/`buildPlotSpec()`; `ViewModel`; menus sync from it. *Built, `13516ce2`.* |
| B | `StreamCatalog` and `openLog()` in the core; calibration through `sensoranalysis_core_json`; `SensorLog`/`SensorStream` retired. *Built, `cc3ca1c3`.* |
| C | `PreferenceStore` with protobuf JSON, byte-compatible. *Built, `88830bd3`.* |
| C2 | `sensorviz_core` built without Qt, in its own directory; the dumping half of `session_cli`, the comparison oracle for D-F |
| D+E | `Transform`/`TransformRegistry`; port magnitudes, altitude and compass (onto `sensoranalysis_core`, with the decision record superseding part of 0030); one generic Qt parameter dialog |
| F | `Session` facade, `hover()`/`formatTime()` in the core; `MainWindow` reduced to menus, dialogs and widgets; the rest of `session_cli` |

**Revised 2026-10-28**, after phases B and C:

- **C2 comes first.** `sensorviz_core` was defined after `find_package(Qt6
  REQUIRED)` in sensorviz's CMakeLists, so it could not be built without Qt,
  and the C interface (step 3 of [leaving Qt](leaving-qt.md)) needs it in a
  Qt-free build. The capture suites show only default settings, so before D
  touches declination, battery forward and sea-level pressure, a dump of every
  stream, hover row and plot spec under scripted setting changes is the
  before-and-after check.
- **D and E are one phase.** The Euler conversion E was to copy
  (`compasscore::displayAngles()`, parity-checked against Qt) was built for
  Tag Calibrator, so the compass transform goes onto the core directly
  instead of being ported twice.
- **The activity filter is not ported.** It stays in the Qt sensorViz as it
  is, outside the registry, and goes when that app retires; the Rust app will
  not have it. Its prompt-on-enable dialog is the one parameter UI that does
  not fit a generic form, and it is little used.
- **`ViewModel` keeps the stream list.** Transforms add and remove derived
  streams through it; the catalog stays what `openLog()` read. Moving the
  list, as the sketch has it, changes nothing visible.

C2 needs C. D+E needs C2. F needs all of them.

**Done means:**

- No file in `sensorviz_core` includes a Qt header.
- `MainWindow` contains no computation on sample values, except the activity
  filter, which leaves with the Qt app.
- `session_cli` reproduces the `PlotSpec` the Qt app draws for every fixture.

## Risks

- **Bit compatibility at three seams:** Euler angles (phase E, parity check),
  calibration JSON numbers (phase B, `sensoranalysis_core_json` already
  matches Qt) and preference bytes (phase C, round-trip check).
- **Paths on Windows.** `QString` paths are UTF-16 today. The core takes
  UTF-8 `std::string` and opens SQLite with UTF-8 names, which SQLite expects.
  The Qt layer converts with `QString::toUtf8()`. Test a non-ASCII path on
  Windows.
- **The size of the change.** Phases B–F touch about 1,900 lines of domain
  logic and most of `MainWindow`. Small phases, each pixel-checked, keep any
  regression attributable.

## Open questions

- Should `info` and event reading move into `sensordata` so other tools get
  them, or stay in `StreamCatalog`?
- Should hover and time formatting be localisable? Today's formats are fixed
  and English. The core would keep them fixed unless a front end needs
  otherwise.
- Should the compass visualizer move behind a small "orientation at time t"
  core call, so a webview could draw its own? It is out of scope here.
