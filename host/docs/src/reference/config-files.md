# Configuration Files

Tag Monitor can save the configuration shown in the **Configuration** tab to a
file and restore it later. Use this to apply one set of schedule and sensor
settings to a batch of tags without re-entering it for each one, and to keep a
record of how the tags in an experiment were configured.

## Saving and Restoring

The Configuration tab carries a **Save configuration to file** button and a
matching restore button. Both open a file dialog filtered to `*.json` and
default to your home directory.

Restore only replaces the values shown in the tab. Nothing reaches the tag until
you write the configuration to it as usual, so you can restore a file, adjust a
date, and then write.

## File Format

The file is the `Config` message from `proto/tagdata.proto`, written as
protobuf JSON. Three choices in how Tag Monitor writes it are worth knowing:

- Field names are the proto field names, not lowerCamelCase.
- Default-valued fields are written out rather than omitted, so a saved file
  shows the whole configuration, including the parts you left alone.
- The output is indented, so it is readable and diffable.

Every section of the Configuration tab has a corresponding section in the file.

Times are Unix epochs — seconds since 1 January 1970, UTC. A tool such as
[Epoch Converter](https://www.epochconverter.com) will turn one back into a
date if you are reading a file by hand.

## Fields

The authoritative definition is `Config` in `proto/tagdata.proto`; this is a
summary of what the top level holds.

| Field | Meaning |
| --- | --- |
| `tag_type` | Which tag family the configuration is for: `BITTAG`, `BITTAG_LE`, `PRESTAG`, `BITTAGNG`, `BITPRESTAG`, `COMPASSTAG`, `IMUTAG`, `UIUCTAG` |
| `active_interval` | `start_epoch` and `end_epoch` for the logging window |
| `hibernate` | Zero or more intervals inside the active window during which the tag sleeps |
| `period` | Period for synchronous tags |
| `start_delay` | Delay before logging starts, in minutes |
| `bittag_log` | BitTag log format: `BITTAG_BITPERSEC`, `BITTAG_BITSPERMIN`, `BITTAG_BITSPERFOURMIN`, `BITTAG_BITSPERFIVEMIN` |
| `adxl362` | ADXL362/367 accelerometer settings |
| `lsm6` | LSM6DSV accelerometer and gyroscope settings |

A field that does not apply to the selected tag type is still written, holding
its default value.

## Example

A classic BitTag configured to log bits per five minutes across two days, with
two hibernation windows:

```json
{
  "tag_type": "BITTAG",
  "active_interval": {
    "start_epoch": 1606230000,
    "end_epoch": 1606406400
  },
  "hibernate": [
    {
      "start_epoch": 1606233600,
      "end_epoch": 1606237200
    },
    {
      "start_epoch": 1606244400,
      "end_epoch": 1606248000
    }
  ],
  "bittag_log": "BITTAG_BITSPERFIVEMIN",
  "adxl362": {
    "range": "R4G",
    "freq": "S50",
    "filter": "AAquarter",
    "act_thresh_g": 0.35,
    "inact_thresh_g": 0.35,
    "inactive_sec": 0.5
  }
}
```

A file written by Tag Monitor is longer than this, because it includes the
default-valued fields that this example leaves out. Both forms restore.
