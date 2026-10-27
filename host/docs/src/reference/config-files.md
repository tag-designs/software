# Configuration Files

Tag Monitor can save the configuration shown in the **Configuration** tab to a
file and open it later. Use this to apply one set of schedule and sensor
settings to a batch of tags without re-entering it for each one, and to keep a
record of how the tags in an experiment were configured.

## Saving and Opening

**File ▸ Save Configuration…** asks for a file, offering the one the
configuration came from, and writes it. **File ▸ Open
Configuration…** opens a file, and **File ▸ Open Recent** lists the last ten
files opened or saved. These are also on the Configuration tab's popup menu.
No tag needs to be attached.

Opening a file makes it the configuration being edited, for the tag type the
file names. Nothing reaches a tag until you start one, so you can open a file,
adjust a date, attach a tag of that type, and start it.

A file does not have to list every setting:

- A setting the file leaves out takes the tag type's default value, so a file
  can hold just the settings you care about, for example only `act_thresh_g`.
- A setting the file states is used, including a zero.
- A file that does not name a tag type (`tag_type`) is opened for a tag type you
  choose; the attached tag's type is offered first.
- Settings a tag type does not have are ignored, including the defaults Tag
  Monitor writes into a saved file for settings that type lacks.

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
default-valued fields that this example leaves out. Both forms open.
