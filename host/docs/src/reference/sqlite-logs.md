# SQLite Log Format

Ultralight Tags can store downloaded data in SQLite database files with the
`.db3` extension. These files are produced by `tag-dwnld` and by host download
workflows that use the same SQLite writer.

Each database contains a small set of common tables plus the sensor tables for
the downloaded tag type. Start with the `streams` table when you want to find
the measurements in a file: it lists each available stream, the table and
column that store it, and the units for the value.

## Tools For Inspecting Logs

You can inspect `.db3` files with any SQLite-compatible tool.

- [DB Browser for SQLite](https://sqlitebrowser.org/) is a desktop application
  for opening tables, browsing rows, and running SQL queries without using the
  command line.
- The `sqlite3` command-line tool is useful for quick checks and scripted
  analysis.
- Python, R, MATLAB, and many notebook environments can read SQLite databases
  directly.

## Quick Command-Line Checks

List the tables in a database:

```sh
sqlite3 my-log.db3 ".tables"
```

List the sensor streams described by the database:

```sh
sqlite3 my-log.db3 \
  "select stream_id, table_name, time_column, value_column, units from streams order by stream_id;"
```

Show the tag type and UUID:

```sh
sqlite3 my-log.db3 \
  "select fieldname, value from info where fieldname in ('tagtype', 'uuid');"
```

## Common Tables

Every SQLite log has common metadata tables. The sensor tables vary by tag
type.

| Table | Purpose |
| --- | --- |
| `schema_info` | Describes the database schema version and the software component that produced the file. |
| `info` | Stores tag metadata as field/value pairs, including tag type, UUID, tag info JSON, and configuration JSON. |
| `states` | Stores state-history snapshots reported by the tag around the collection and download. |
| `streams` | Maps measurements to sensor tables, time columns, value columns, display names, units, and quantities. |

### `schema_info`

`schema_info` is a key/value table for database-level metadata.

| Column | Meaning |
| --- | --- |
| `key` | Metadata key. Current files include `log_schema_version` and `producer`. |
| `value` | Metadata value as text. |

### `info`

`info` stores flexible field/value metadata. Common fields include:

| Field | Meaning |
| --- | --- |
| `tagtype` | Tag family name recorded by the downloader. |
| `uuid` | Tag UUID. |
| `info` | JSON copy of the tag information returned by the device. |
| `config` | JSON copy of the tag configuration used for the download. |
| `bittag_log` | BitTag log packing mode, present for classic BitTag logs. |

The JSON fields are useful when an analysis script needs configuration details
that are not promoted into their own columns.

The JSON values in `info`, `config`, and calibration rows are generated from
the same Protobuf messages used by the host/tag protocol. Field names follow
the schema names from the repository's `proto/` directory. Use
`proto/tag.proto` for tag configuration, state, and metadata messages. Use
`proto/tagdata.proto` for log payload messages and `CalibrationConstants` when
you need to interpret nested data or calibration JSON fields.

### `states`

`states` records tag state-history entries. These rows are separate from sensor
samples.

| Column | Meaning |
| --- | --- |
| `Epoch` | UTC Unix timestamp in seconds. |
| `State` | Tag state name, such as a running or terminal state. |
| `EntryCode` | Reason reported for entering that state. |
| `Temperature` | Tag-reported internal temperature in degrees C. |
| `Voltage` | Tag supply voltage in V. |
| `InternalLogSize` | Count of internal log records or pages reported by the tag. |
| `ExternalLogSize` | Count of external log records or pages reported by the tag. |

### `streams`

`streams` is the best table for discovering usable data. It tells viewers and
analysis scripts where each measurement lives.

| Column | Meaning |
| --- | --- |
| `stream_id` | Stable programmatic name for the measurement. |
| `group_id` | Optional group for related columns, such as the columns in a compass record. |
| `group_name` | Human-readable name for the group. |
| `table_name` | Table containing the data. |
| `time_column` | Column to use as the x-axis or sample time. |
| `value_column` | Column containing this stream's value. |
| `stream_kind` | Kind of stream. Current logs use `scalar` and `record_column`. |
| `display_name` | Human-readable measurement name. |
| `units` | Engineering units for the value, when applicable. |
| `quantity` | General physical quantity, such as pressure, voltage, or acceleration. |
| `comment` | Short description of the stream. |

`scalar` streams can be plotted directly as one value against one time column.
`record_column` streams are columns in a wider record table. For example,
CompassTag stores acceleration and magnetic-field columns in one `Compass`
table, and `streams` describes each column separately.

## Time Columns

Most tag families use `Epoch` as their time column. `Epoch` is a UTC Unix
timestamp in seconds.

IMUTag high-rate tables use elapsed collection time instead:

| Column | Meaning |
| --- | --- |
| `ElapsedUs` | Corrected elapsed microseconds from the start of the retained collection. This is the ordinary time column for IMUTag sensor streams. |
| `RawElapsedUs` | Nominal elapsed microseconds before clock correction. |
| `SegmentId` | Timing segment containing the row. A new segment starts at collection start and after explicit resync events. |
| `SampleIndex` | Sample index within the segment. |

The IMUTag clock correction comes from `ppm_clock_error` in the `info` JSON
field. The downloader copies the value it actually used into
`ImuSegment.CorrectionPpm` for each timing segment. Older firmware may omit
`ppm_clock_error`; in that case the downloader uses `0` ppm.

Within a segment, `RawElapsedUs` is computed from sample count and configured
sample rate. `ElapsedUs` applies the segment's ppm correction:

```text
ElapsedUs = segment_start_us
          + round(RawElapsedUs * (1 + CorrectionPpm / 1000000.0))
```

Use `ElapsedUs` for ordinary plotting and analysis. Use `RawElapsedUs`,
`CorrectionPpm`, and the segment tables when you need to audit or rebuild the
timing model.

To convert IMUTag elapsed time to seconds:

```sql
select ElapsedUs / 1000000.0 as seconds, ax, ay, az
from ImuAccel
order by ElapsedUs;
```

## Shared Sensor Tables

Several tag families use the same simple table shapes. A table appears only
when the tag type records that measurement.

| Table | Columns | Units | Meaning |
| --- | --- | --- | --- |
| `Voltage` | `Epoch`, `Voltage` | V | Tag supply voltage. |
| `CoreTemperature` | `Epoch`, `Temperature` | C | Internal tag temperature. |
| `Activity` | `Epoch`, `Activity` | % | Activity value over the tag's sample bucket. |
| `Pressure` | `Epoch`, `Pressure` | mbar | Absolute pressure from the pressure sensor. |
| `Temperature` | `Epoch`, `Temperature` | C | Temperature reported by the pressure sensor. |

`CoreTemperature` and `Temperature` are different measurements. Use
`CoreTemperature` for the tag's internal temperature and `Temperature` for the
pressure sensor's temperature.

## BitTag, BitTag LE, And BitTagNG

BitTag-family logs contain:

| Table | Contents |
| --- | --- |
| `Voltage` | One voltage row for each downloaded log entry or page. |
| `CoreTemperature` | One internal-temperature row for each downloaded log entry or page. |
| `Activity` | Activity rows unpacked from compact activity buckets. |

Classic BitTag logs use the `bittag_log` value in `info` to describe how
activity was packed. BitTagNG activity is stored as 15-second buckets. In both
cases, read the expanded `Activity` table for analysis rather than trying to
interpret the compact field yourself.

Example:

```sql
select Epoch, Activity
from Activity
order by Epoch;
```

## PresTag

PresTag logs contain:

| Table | Contents |
| --- | --- |
| `Voltage` | Tag supply voltage. |
| `Pressure` | Absolute pressure samples in mbar. |
| `Temperature` | Pressure-sensor temperature samples in degrees C. |

Samples are written using the sample period from the tag configuration. Raw
PresTag downloads are converted to engineering units before rows are written.

Example:

```sql
select Epoch, Pressure
from Pressure
order by Epoch;
```

## BitPresTag

BitPresTag logs combine activity and pressure-family measurements.

| Table | Contents |
| --- | --- |
| `Voltage` | Tag supply voltage. |
| `Activity` | Activity rows unpacked from compact 15-second buckets. |
| `Pressure` | Absolute pressure samples in mbar. |
| `Temperature` | Pressure-sensor temperature samples in degrees C. |

Example:

```sql
select p.Epoch, p.Pressure, t.Temperature
from Pressure as p
join Temperature as t on t.Epoch = p.Epoch
order by p.Epoch;
```

## CompassTag

CompassTag logs contain the BitTag-style summary streams plus a raw compass
record table and calibration history.

| Table | Contents |
| --- | --- |
| `Voltage` | Tag supply voltage. |
| `CoreTemperature` | Internal tag temperature. |
| `Activity` | Activity samples. |
| `Compass` | Raw accelerometer and magnetometer record columns. |
| `Calibration` | JSON calibration constants with their timestamps. |

`Compass` has these columns:

| Column | Units | Meaning |
| --- | --- | --- |
| `Epoch` | seconds | UTC Unix timestamp. |
| `ax`, `ay`, `az` | mg | Raw accelerometer axes used for orientation. |
| `mx`, `my`, `mz` | uT | Raw magnetometer axes used for orientation. |

SensorViz uses `Compass` and `Calibration` together to generate heading,
pitch, roll, dip, field-strength, and acceleration views.

Example:

```sql
select Epoch, mx, my, mz
from Compass
order by Epoch;
```

## IMUTag

IMUTag logs use elapsed-time tables because the high-rate IMU samples are
reconstructed from raw pages, timing anchors, configured sample rate, and clock
correction metadata.

| Table | Contents |
| --- | --- |
| `ImuHeader` | One row for each downloaded IMUTag page header. Preserves the RTC timestamp anchor and raw header flags. |
| `ImuSegment` | Collection-start and resync timing segments used to reconstruct elapsed sample time. |
| `ImuEvent` | Resync and restart markers for viewers and analysis scripts. |
| `ImuAccel` | High-rate accelerometer samples in mg. |
| `ImuGyro` | High-rate gyroscope samples in dps. |
| `ImuMag` | Lower-rate magnetometer samples in uT. |
| `ImuPressure` | Lower-rate pressure samples in mbar. |
| `ImuTemperature` | Pressure-sensor temperature samples in degrees C. |
| `Calibration` | JSON calibration constants with their timestamps. |

The sample tables share timing columns:

| Column | Meaning |
| --- | --- |
| `SegmentId` | Timing segment containing the sample. |
| `SampleIndex` | Nominal sample index within the segment. |
| `RawElapsedUs` | Uncorrected elapsed microseconds. |
| `ElapsedUs` | Corrected elapsed microseconds used by SensorViz. |

`ImuAccel`, `ImuGyro`, and `ImuMag` then add axis columns:

| Table | Value columns |
| --- | --- |
| `ImuAccel` | `ax`, `ay`, `az` |
| `ImuGyro` | `gx`, `gy`, `gz` |
| `ImuMag` | `mx`, `my`, `mz` |

`ImuPressure` adds `Pressure`, and `ImuTemperature` adds `Temperature`.

For most analysis, use `ElapsedUs` as the sample time. Keep `ImuHeader`,
`ImuSegment`, and `ImuEvent` when you need to inspect timing anchors, restarts,
or storage skips.

### IMUTag Timing Segments

An IMUTag segment is a continuous run of samples whose timing can be computed
from one anchor point, the configured IMU output data rate, the sample count,
and the clock correction. Ordinary page headers are still stored in
`ImuHeader`, but they are treated as diagnostic checkpoints. They do not
normally re-anchor high-rate samples because their rounded millisecond values
can introduce page-to-page jitter.

New segments begin at collection start and after explicit resync boundaries.
Those boundaries preserve useful context when continuity may have been
disrupted. Examples include recovery after a monitor/debug connection resets a
running tag, or recovery after storage readback finds an erased, invalid, or
unreadable page while restoring log cursors. A storage skip can also mark a
resync when firmware had to skip external storage after a write failure.

`ImuSegment.Event` records why the segment began, such as `COLLECTION_START`,
`RESYNC`, `RESYNC_STORAGE_SKIP`, or `RESTART_RECOVERY`. `ImuEvent` stores the
same discontinuities as viewer markers. When a resync anchor would place the
new segment before samples already written for the previous segment, the
downloader keeps elapsed timestamps monotonic by starting the new segment no
earlier than the next expected sample boundary.

Example:

```sql
select ElapsedUs / 1000000.0 as seconds, gx, gy, gz
from ImuGyro
order by ElapsedUs;
```

## Calibration Table

CompassTag and IMUTag logs include `Calibration` when calibration history is
available.

| Column | Meaning |
| --- | --- |
| `Epoch` | UTC Unix timestamp in seconds for the calibration entry. |
| `Constants` | JSON calibration constants generated from the `CalibrationConstants` Protobuf message in `proto/tagdata.proto`. |

Use the newest calibration row when you need the same default behavior as
SensorViz. Keep older rows if you need to audit how calibration changed over
time.

## External Analysis Notes

- Prefer `streams` over hard-coded table names when possible.
- Use `units` from `streams` so scripts label exported data correctly.
- Treat SensorViz preferences as display settings, not as database schema.
- Processed database copies may add derived tables and derived stream rows.
  Re-read `streams` after processing instead of assuming only raw streams are
  present.

## Related Pages

- [tag-dwnld](../cli/tag-dwnld.md) explains how to create SQLite downloads from
  the command line.
- [SensorViz](../apps/sensorviz.md) explains how to load and inspect SQLite
  logs visually.
- [dataprocessing](../cli/dataprocessing.md) explains how processed database
  copies add derived streams.
