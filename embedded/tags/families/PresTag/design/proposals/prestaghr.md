---
type: proposal
status: superseded
superseded-by: docs/decisions/0047-prestaghr-full-resolution-pressure.md
summary: "PresTagHR -- a PresTag variant that logs the LPS27's full 24-bit pressure in 5-byte packed records, with low-noise mode as a stored-configuration option, sized for 20 s sampling over 3-5 months."
---

# PresTagHR: Full-Resolution Pressure Logging

> **Built (2026-11-09).** What was decided is
> [decision 0047](../../../../../../docs/decisions/0047-prestaghr-full-resolution-pressure.md);
> how it works now is the [family README](../../README.md#prestaghr); what is
> still open is in the [TODO](../../../../../../TODO.md#prestag). Two details
> changed in building: `Config.lps27` is field 11, not 10, which the
> [tag-reported presence proposal](../../../../../../docs/shared/design/proposals/tag-reported-config-presence.md)
> reserves; and Tag Monitor offers the setting only when the tag reports
> `lps27`, since the config shape is shared by every PresTag variant.

PresTagHR is a third member of the PresTag family. It stores every sample as the
LPS27HHW's five output bytes -- 24-bit pressure and 16-bit temperature -- in
240-byte pages of 48 samples. It exports them as a new packed-binary protobuf
payload, `PresTagHRLog`. It lets the user turn on the sensor's low-noise mode in the stored configuration. The target use is
the altitude at which birds stay while feeding. A typical run samples every 20 s
for 3-5 months, and 4 MB holds 194 days at that rate.

## Why

PresTag keeps the top 16 bits of the 24-bit pressure: the driver reads 4 bytes
starting at `PRESS_OUT_L` (0x29) and drops `PRESS_OUT_XL` (0x28). One step is
1/16 hPa = 6.25 Pa, about 0.5 m of altitude, and the rounding alone adds
6.25 / sqrt(12) = 1.8 Pa RMS. With 24 bits the step is 1/4096 hPa, about 2 mm,
so only the sensor's own noise limits the reading. Temperature is already stored in full.

## Record format

A sample is the five register bytes in the order the sensor delivers them, read
in one burst from 0x28: `PRESS_OUT_XL, PRESS_OUT_L, PRESS_OUT_H, TEMP_OUT_L,
TEMP_OUT_H`.

```c
#define PRESTAG_HR_LOG_SAMPLES 48
#define PRESTAG_HR_PRESSURE_LSB_PER_HPA 4096
#define PRESTAG_HR_PRESSURE_FAILED 0x800000   /* 24-bit INT_MIN: read failed */

#pragma pack(push, 1)
typedef struct {
    uint8_t pressure[3];   /* signed 24-bit, little-endian, 4096 LSB/hPa */
    int16_t temperature;   /* 100 LSB/degC */
} t_PresTagHRSample;                                  /* 5 bytes   */

typedef struct {
    t_PresTagHRSample data[PRESTAG_HR_LOG_SAMPLES];
} t_PresTagHRDataLog;                                 /* 240 bytes */

typedef struct {
    int32_t epoch;
    float voltage;
    float temperature;
    t_PresTagHRDataLog samples;
} t_PresTagHRBlock;                                   /* 252 bytes */
#pragma pack(pop)
```

- **The page stays 240 bytes.** 48 x 5 = 60 x 4, so erase sizing, header-to-page
  pairing (header *i* owns bytes `[i*240, (i+1)*240)`), `readExternalPage()` and
  download paging are unchanged. Only the write address changes:
  `external_blocks * 5` instead of `* 4`. Five-byte writes cross 256-byte
  program pages, which `at25xe.c` already splits.
- **End of page:** erased flash reads 0xFFFFFF, which sign-extends to -1, the
  same `PRESTAG_RAW_PRESSURE_END` rule as today.
- **Failed read:** 0x800000, the 24-bit counterpart of the `SHRT_MIN` PresTag
  writes. It is logged, not dropped, as in PresTag.
- The structs go in `include/prestag_log_format.h` under the
  [shared binary log rules](../../../../../../docs/shared/binary-datalogs.md).

### Capacity at 20 s

| | PresTagHR | PresTag |
| --- | --- | --- |
| Samples per page | 48 | 60 |
| Samples in 4 MB (17,476 pages) | 838,848 | 1,048,560 |
| Days at 20 s | **194** | 243 |

- **Headers:** 17,476 x 8 bytes = 140 KB, against a persistent region of about
  218 KB in the current image (`0x08009800`-`0x08040000`).
- **Battery:** with the measured fit in
  [power-results.md](../power-results.md#battery-lifetime) (0.2842 uA rest,
  15.26 uC per sample), a 20 s period averages about 1.05 uA. Nominal life is
  219 days on 5.5 mAh and 438 days on 11 mAh. Five months fits on 5.5 mAh with
  about 30% margin before derating. The low-noise mode's cost is not yet
  measured and comes out of that margin.

## Low-noise mode

- **Configuration:** a new optional sensor message, following `Adxl362` and
  `Lsm6dsv`:

  ```proto
  message Lps27 {
    bool low_noise = 1;   // LPS27 CTRL_REG2.LOW_NOISE_EN for each one-shot
  }
  // in Config:
  Lps27 lps27 = 10;
  ```

  `tagdata.options` sets `Config.lps27 type:FT_IGNORE` and `Lps27
  skip_message:true` by default, and only `prestaghr-proto-c` enables them, so
  no other tag's nanopb `Config` changes.
- **Stored config:** `bool lps_low_noise` goes after `internal` in
  `t_storedconfig`, in what is now padding (offset 29). No existing offset
  moves. `readConfig()`/`writeConfig()` translate it under
  `PRESTAG_HR_LOG`. Bump the `stored_config` layout version, and teach
  `capturesource.cc` the new version.
- **Host:** the HR `default-config.json` carries `"lps27": {"low_noise":
  false}`. A config shape is built from that file, so only PresTagHR offers
  the setting. The Tag Monitor config form
  (`tagmonitor-rs/core/src/model/configform.rs`) needs a checkbox shown when the
  shape contains `lps27.low_noise`. The setting has to reach the downloaded
  SQLite log with the rest of the configuration; check that it does.
- **Conversion time:** low-noise mode probably makes the one-shot take longer.
  PresTag waits one 5 ms poll (`LPS27_READY_POLLS 1`). If the conversion takes longer, every sample fails
  and is logged as 0x800000. PresTagHR sets `LPS27_READY_POLLS` to a few
  polls. The loop leaves as soon as the data is ready, so this costs nothing in
  low-power mode. Measure the actual conversion time on the bench before
  settling the number.

## Driver

Add a new function beside the existing one, and leave `lps27GetPressureTemp()`
untouched:

```c
/* Trigger one conversion and read the five output registers from 0x28. */
bool lps27GetPressureTempFull(const TagPressureDevice *device, bool low_noise,
                              uint8_t raw[5]);
float lps27PressureFull(int32_t pressure_lsb);   /* hPa */
```

This leaves the compiled code of PresTag, PresTagRaw, BitPresTag and
BitPresTagMX25R identical, so none of them needs requalifying. The cost is a
second copy of the one-shot sequence (about 40 lines).

**Rejected for now: one full-read routine with the old function as a wrapper.**
The wrapper would drop the low byte (shift right by 8), so it is bit-identical
in output. However, it changes the sensor transaction (5 bytes instead of 4) in
four qualified images, so it would have to ride a batched firmware
qualification. Fold the two together then, if it is wanted.

## Firmware changes

All behind `PRESTAG_HR_LOG`, in the pattern `PRESTAG_RAW_LOG` set:

| File | Change |
| --- | --- |
| `families/PresTag/inc/datalog.h` | `DATALOG_SAMPLES` = 48, `t_DataLog` = `t_PresTagHRDataLog`, sample size 5 |
| `families/PresTag/src/state_run.c` | read with `lps27GetPressureTempFull(..., sconfig.lps_low_noise, ...)`; write 5 bytes |
| `families/PresTag/src/datalog.c` | `writeDataLog()` takes bytes and addresses by sample size; `data_logAck()` packs `t_PresTagHRBlock`s into `PresTagHRLog.samples`; static asserts 48 x 5 = 240 and the nanopb buffer |
| `families/PresTag/inc/tag_identity_family.h` | decoder `prestag_hr`, 48 samples per page, 5 bytes per sample, pressure scale 4096 |
| `families/PresTag/inc/config.h`, `src/config.c` | `lps_low_noise`, as above |
| `PresTagHR/` (new) | `custom.h` from PresTag with `FIRMWARE_STRING` "PresTagHRv1" and more ready polls; `project.mk` with `-DPRESTAG_HR_LOG=1`; `CMakeLists.txt` with `DISTRIBUTE` |
| `embedded/tags/CMakeLists.txt`, `BUILD_SOURCES.md` | register the target |
| `embedded/proto-c/prestaghr-proto-c/` (new) | overrides enabling `Ack.prestag_hr_data_log`, `PresTagHRLog`, `Config.lps27`, `Lps27`; `default-config.json` with `period` 20 |

Keep the page-boundary rules in `state_run.c`: hibernation on a page boundary,
the drift check that starts a new page, and the cursor round-up after a reset.
They are written in `DATALOG_SAMPLES` and carry over unchanged.

## Protocol and host

- **Protobuf:** `message PresTagHRLog { bytes samples = 1; }` (packed
  `t_PresTagHRBlock`s; the scales are fixed in the header) and a new `Ack`
  payload field `prestag_hr_data_log`. Use a new message, not `PresTagRawLog`:
  an older host would otherwise decode HR blocks as 4-byte samples without
  error. Set `PresTagHRLog.samples max_size:3780`, 15 blocks per reply as
  PresTagRaw sends.
- **SQLite:** `dumpPresTagHRLog()` in `tagcore/sqlitelog/pressure.cc`,
  dispatched from `sqlitelog.cc` (update the recipe comment there). Copy each
  block out of the payload, never cast it; sign-extend the 24-bit pressure; skip
  nothing but the -1 end. It writes the existing Pressure, Temperature and
  Voltage tables, so sensorViz and downstream tools need no change.
- **Offline rebuild from a capture:** generalise `PresTagDecoder` in
  `tagcore/recovery/capturesource.cc` to take the sample size and samples per
  page, and register `prestag_hr`. A five-month field run is the case a
  failed-tag capture has to recover.
- **Python:** `tagclient/examples/tag_dwnld.py`, the tagcore-py offline tests,
  and the config-shape code (`configshape.h`, `configshape.rs`,
  `configshape.py`). These treat several default configs with one tag type as
  a union, and that now covers three.

## Verification

Without hardware:

1. Build the family host simulation (`families/PresTag/test/`) with
   `-DPRESTAG_HR_LOG=1`. Cover full pages, the partial last page, a drift-skip
   page, the end of flash, and a failed read.
2. A tagcore round-trip test: sign extension of positive and negative 24-bit
   values, the -1 end, 0x800000.
3. Doxygen warning diff on the changed files; `git diff --check`.

On the bench, following the [flashing protocol](../../../../../../AGENTS.md#firmware-and-bench-work):

1. `tag-test` after flashing.
2. Measure the one-shot conversion time in each mode, then set
   `LPS27_READY_POLLS`.
3. Run at 20 s in each mode. Download, then extend `prestag_check_download.py`
   to check HR downloads: schedule, gaps, no failed reads.
4. Take Joulescope points at 20 s and 90 s in each mode with
   `prestag_power_model.py`, and record them in
   [power-results.md](../power-results.md). Recompute the 5-month margin.
5. Run a PresTag and a PresTagHR side by side for an hour. HR sample-to-sample
   noise should fall from the 1.8 Pa rounding floor to the sensor's own,
   lower in low-noise mode.
6. Add a PresTagHR profile (`prestaghr-20.json`) to `tag_release_check.py`.

## Documentation on completion

Turn this into a `design` section of the family README (format, capacity,
low-noise). Write a decision record for the 5-byte record and the separate
driver function, then mark this proposal `historical`. Update:

- `docs/shared/binary-datalogs.md`;
- `embedded/tags/design/custom-defines.md`;
- `host/docs/src/reference/sqlite-logs.md`;
- the user manual's target list.

## Decided

- Five-byte records (2026-11-09): six bytes would only pad the 16-bit
  temperature and would cut capacity at 20 s to 162 days.
- Low-noise mode is a stored-configuration option, not a build option
  (2026-11-09).
- Capture rebuild is in the first release (2026-11-09).

## Open

- The driver approach above: a separate function (recommended), or the wrapper
  folded into the next batched qualification.
