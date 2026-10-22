---
type: proposal
status: proposed
summary: Record, inside each SQLite log, the few deployment facts that change how it is processed - location, time zone, mounting, on-animal interval, elevation or reference pressure, and an optional declination - all optional, read and written through a Qt-free tagcore layer, edited with a small host app (tagmeta) and a command-line twin, and used as defaults by sensorviz, the filter and dataprocessing.
---

# Deployment Metadata for Processing

**Status:** proposed, nothing built.

A log should carry the few facts about its deployment that change how it is
**processed**, so that every tool processes it the same way without being
told again. The proposal has four parts:

1. **Storage.** A `Deployment` table in the `.db3`, with every field optional.
2. **A Qt-free tagcore layer** to read, validate and write it.
3. **Editing:** a small Qt app, `tagmeta`, and its command-line twin,
   `tag-meta`.
4. **Consumers:** sensorviz, the post-processing filter and `dataprocessing`
   read the fields as defaults. A value given on a command line or in a UI
   still overrides.

**Out of scope:** everything else about an experiment, such as species,
animal and band IDs, permits, study design and notes. Researchers keep those
in their own systems.

## What the tools need, and why

| Field | Unit / form | Used for | Precision needed |
| --- | --- | --- | --- |
| `latitude_deg`, `longitude_deg` | degrees, WGS84 | The World Magnetic Model field: declination for heading, the dip and field magnitude the filter expects. Sun position, for anyone computing day and night | Whatever the researchers choose to give; the tools need only about 0.01° (~1 km) |
| `time_zone` | IANA name, e.g. `Europe/London` | Showing and exporting in local time, with daylight saving right; a fixed offset gets DST wrong for half the year | — |
| `mounting_attitude` | quaternion `w x y z`, the tag frame's attitude relative to the animal's body frame | Turning tag attitude into body attitude and heading. It replaces sensorviz's "battery forward" switch | — |
| `attached_utc`, `removed_utc` | ISO 8601 UTC | The on-animal interval: excluding bench and handling time, choosing the still periods the filter starts from, shading in sensorviz | Minutes |
| `site_elevation_m` | metres above sea level | Absolute altitude from pressure. Relative altitude needs neither this nor the next field | ±10 m (≈ ±1.2 hPa) |
| `reference_pressure_hpa`, `reference_pressure_utc` | hPa at a time, at the site | The alternative to elevation: a measured pressure for the altitude datum | — |
| `declination_deg` | degrees, east positive | Overrides the model's declination when a researcher prefers a measured value | — |

**Mounting: simple to choose, general to store.** Tags are mounted on the
animal's back, so researchers choose only between **battery forward** and
**battery backward**. The tools store the choice as the tag's attitude
relative to the animal's body:

- battery forward is the identity;
- battery backward is a 180° turn about the vertical.

Every consumer works from that quaternion, never from the label. A later
option (a tag on the side, say, or a measured tilt) is just another
quaternion, with no change to the storage or to any tool that reads it.

**Not on the list because the log already has them:** tag identity,
firmware, sensor configuration, calibration constants, start and stop, the
state log, and clock error.

## Storage

```sql
CREATE TABLE Deployment (
    Key     TEXT PRIMARY KEY,  -- a field name above
    Value   TEXT NOT NULL,     -- the value as text, in the field's unit
    Unit    TEXT,              -- e.g. 'deg', 'm', 'hPa'; NULL for names and times
    SetUtc  TEXT NOT NULL,     -- when it was set
    SetBy   TEXT NOT NULL      -- the tool and version that set it
);
```

**A key-value table, every row optional:**

- **Absent means "not known".** No tool may require a field. Each falls back
  to what it does today: a UTC offset of 0, battery forward, the whole log,
  and so on.
- **Adding a field later** is a new key, not a schema change.
- **Logs without the table** are ordinary logs. Readers treat a missing table
  as an empty one, so `schema_info` stays at version 1 and older tools ignore
  the table.
- **Not written at download.** `tag-dwnld` and qtmonitor do not write it
  (see [Later](#later)). It is created by the first edit.
- **Documented** in the user manual's
  [SQLite logs reference](../../../../docs/src/reference/sqlite-logs.md), so
  external analysis tools can read it.

## The tagcore layer

`deployment.h` / `deployment.cc` in tagcore, Qt-free and C++17, beside the
SQLite writer:

```cpp
namespace tagcore {

struct DeploymentField {
    std::string key, value, unit, setUtc, setBy;
};

struct DeploymentInfo {               // parsed, typed view; every member optional
    std::optional<double> latitudeDeg, longitudeDeg;
    std::optional<std::string> timeZone;            // IANA name
    std::optional<std::array<double, 4>> mountingAttitude;  // w x y z, tag frame -> body frame
    std::optional<int64_t> attachedUnix, removedUnix;
    std::optional<double> siteElevationM;
    std::optional<double> referencePressureHpa;
    std::optional<int64_t> referencePressureUnix;
    std::optional<double> declinationDeg;
};

std::optional<DeploymentInfo> readDeployment(const std::string &dbPath, std::string &error);
std::vector<DeploymentField> readDeploymentFields(const std::string &dbPath, std::string &error);

// Validates, then replaces the given fields (and only those) in one transaction.
bool writeDeployment(const std::string &dbPath, const std::vector<DeploymentField> &set,
                     const std::vector<std::string> &clear, std::vector<std::string> &problems);

std::vector<std::string> validateDeployment(const DeploymentInfo &, const LogTimeRange &);

}  // namespace tagcore
```

**Validation:**

- **Errors**, which refuse the write:
  - a malformed value;
  - latitude outside ±90 or longitude outside ±180;
  - a mounting attitude that is not a unit quaternion;
  - removal before attachment.
- **Warnings**, which allow the write and are shown to the user:
  - the on-animal interval outside the log's time range;
  - an elevation outside −500 to 9,000 m.
- **Location precision** is the researchers' choice. The tools neither round
  nor warn about it.
- **Time-zone names** are checked against the IANA database where one is
  available: Qt's `QTimeZone` in the app, the C++20 `std::chrono` time-zone
  database where the platform has it. Otherwise only the form of the name is
  checked.

**Writing:**

- An edit touches only the `Deployment` table, inside one transaction. The
  sensor tables are never opened for writing, so an interrupted edit leaves
  the log exactly as it was.
- `tagmeta` and `tag-meta` are the only tools besides a download that change
  a `.db3`. sensorviz keeps opening logs read-only.

## Editing

**`tag-meta`** (command line, `host/commandline`):

```bash
tag-meta show log.db3
```

```bash
tag-meta set --lat 51.48 --lon -0.00 --tz Europe/London --mounting battery-forward log.db3
```

```bash
tag-meta copy --from first.db3 second.db3 third.db3
```

- `set` writes the fields given; `--clear KEY` removes one; `copy` applies
  one log's fields to many, for a deployment's whole batch of tags.
- `--mounting battery-forward|battery-backward` writes the matching
  quaternion. `--mounting-attitude W X Y Z` writes any other, for later
  mountings and for tools.
- `show` prints the fields with their units and who set them, plus any
  validation warnings, so it doubles as a checker.
- With `--json`, output and input are JSON, for scripting.

**`tagmeta`** (Qt Widgets, `host/applications/tagmeta`) is a single window
that opens one or more `.db3` files:

- **The form:** one field per row, each with a clear button and its unit,
  grouped as Location, Time, Mounting, On animal and Altitude reference.
  Nothing is required.
- **Location** accepts decimal degrees, at whatever precision is entered.
- **Time zone** is a searchable list of IANA names.
- **Mounting** is a choice between battery forward and battery backward,
  with a small picture of a tag on a back. A stored attitude that is neither,
  set by another tool, is shown as "other (set by …)" and left alone unless
  the user picks one of the two.
- **On-animal interval** shows the log's own start and end beside the
  fields, so a typo stands out.
- **Several files open:** fields that agree show their value; fields that
  differ show "mixed"; saving writes only the fields that were edited.
- **Saving** goes through the same validation as `tag-meta`, showing errors
  and warnings before anything is written.

It is a thin Qt layer over the tagcore functions, in the toolkit-neutral
style of sensorviz's core.

## Consumers

Each consumer reads the fields as defaults; anything set explicitly wins.

| Tool | Reads | Effect |
| --- | --- | --- |
| sensorviz | `time_zone`, `declination_deg` (or the WMM declination at the location and date), `mounting_attitude`, `attached_utc`/`removed_utc` | Local-time axis by default; heading declination; body-frame heading in place of "battery forward"; the on-animal interval shaded, outside it greyed |
| `tagsim filter` (and the C++ port later) | location, `mounting_attitude`, the interval, elevation or reference pressure | The field model without `--site`; body attitude; initialisation from still periods inside the interval; absolute altitude when a datum is known |
| `dataprocessing` | location, `mounting_attitude`, the interval | Orientation processors use the WMM field and declination at the site, and report body-frame orientation |
| tagsim | — (writes them) | Simulated logs get their `Deployment` rows from the source flight (location, time zone, interval) and from its mounting option, as a quaternion, so every consumer is tested on logs that carry them |

## Phases

| Phase | Work | Done when |
| --- | --- | --- |
| 1 | The tagcore layer and `tag-meta`; the table documented in the SQLite logs reference; a check program (validation, round trip, transactional writes that leave sensor tables untouched, logs without the table) | `tag-meta set` / `show` / `copy` work on the fixtures; a log edited and re-read is unchanged apart from `Deployment` |
| 2 | `tagmeta`, the Qt app; a user-manual page | Edits one or many logs; validation shown before saving |
| 3 | Consumers: sensorviz defaults and interval shading; `tagsim filter` and tagsim's generated logs; `dataprocessing` | Each tool picks the fields up with no flags; an explicit flag or UI value still wins |

Phase 1 is independent of everything else; phases 2 and 3 can run in
parallel once it lands.

## Later

- **Entering fields at download time,** through `tag-dwnld` flags or a
  qtmonitor dialog, if researchers ask: they often know the location and
  mounting then.
- **Sun times,** if day and night shading in sensorviz is wanted; the
  location and time zone are what it needs.

## Open questions

- **Which mountings might come next?** Not needed now, since storage and
  consumers already take any attitude, but worth noting when a study needs
  something other than a flat tag on the back.
