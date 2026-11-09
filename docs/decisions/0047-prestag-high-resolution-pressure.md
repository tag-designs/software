---
type: decision
status: accepted
summary: PresTag gains two stored-configuration options -- high resolution, which logs the LPS27's full 24-bit pressure as 5-byte register images, 48 to a 240-byte page, and low-noise conversion -- in one image, with the page geometry following the stored configuration; a separate PresTagHR target or tag type was built and rejected.
---

# 0047. PresTag: high-resolution pressure and low-noise conversion

Date: 2026-11-09

## Context

PresTag keeps the top 16 bits of the LPS27HHW's 24-bit pressure, a step of
1/16 hPa (6.25 Pa, about 0.5 m of altitude). The rounding alone is 1.8 Pa RMS.
Researchers studying the altitude at which birds stay while feeding want the
resolution, at 20 s for 3-5 months. The plan was
[the PresTagHR proposal](../../embedded/tags/families/PresTag/design/proposals/prestaghr.md).

It was first built as a separate target, PresTagHR, sharing PresTag's tag type.
That left one tag type covering firmware with different configurations and
log formats, which the host could tell apart only indirectly. The choice was
then between a new tag type for PresTagHR and one PresTag firmware with the
format as a configuration option; the option was chosen (user, 2026-11-09).

## Decision

- **One PresTag image with two stored-configuration options**, `Config.lps27`
  (field 11; field 10 stays reserved for
  [`config_schema`](../shared/design/proposals/tag-reported-config-presence.md)):
  `high_resolution` and `low_noise`. They are stored as `lps_low_noise` and
  `lps_high_res` at offsets 29 and 30 of `t_storedconfig`, in what was
  padding (stored-config layout 3); only the value 1 counts, so an erased
  configuration reads as off. Both off is PresTag's original behaviour,
  including its original sensor read.
- **High resolution logs five-byte samples, the register image**:
  `PRESS_OUT_XL` through `TEMP_OUT_H` as one burst read returns them
  (`t_PresTagHRSample`, `include/prestag_log_format.h`). 48 x 5 = 240, so the
  page, header-to-page pairing, erase and paging are unchanged. The samples per
  page, 48 or 60, follow the stored configuration and are fixed for a run.
  4 MB holds 838,860 samples, 194 days at 20 s.
- **A high-resolution download is a new message, `PresTagHRLog`** (`Ack`
  field 18), carrying packed `t_PresTagHRBlock` records. The host already
  dispatches on the message, so a download decodes correctly in either format.
- **A failed high-resolution read keeps its slot** as pressure 0x800000. The
  host writes no row for it, so it shows as a gap, not as -2048 hPa.
- **A new driver function, `lps27GetPressureTempFull()`**, takes the noise mode
  per call. `lps27GetPressureTemp()` is unchanged; PresTag uses it when
  neither option is set, so PresTagRaw, BitPresTag and BitPresTagMX25R
  disassemble identically to before. In the default format with low noise,
  the full read's low pressure byte is dropped, which is exactly the 16-bit
  value the original read returns.
- **The host offers the options when the tag reports `lps27`.** PresTag always
  sends it; PresTag up to fw-v0.8 and PresTagRaw never do. A sub-message has
  presence in proto3, so this says which firmware is attached.
- **The capture rebuild takes the format from the captured configuration**
  (`prestag` decoder).

## Rejected

- **A separate PresTagHR target sharing `PRESTAG`** (built first): one tag
  type, two configurations and log formats, distinguishable only indirectly.
- **A new tag type, `PRESTAGHR`**: every switch on `TagType` on the host
  needs it, and users must pick the right image before configuring. One
  configured image leaves the choice to deployment.
- **Six-byte samples** would only pad the 16-bit temperature, and cut 20 s
  capacity to 162 days, too close to five months.
- **The sensor's low-pass filter (oversampling)**: it averages successive
  conversions, so each logged sample would cost several conversions instead
  of one one-shot. Low-noise mode is the noise option (user, 2026-11-09).
  A deployment that needs lower noise than that should use UIUCTag, whose
  BMP585 is markedly quieter, with a collection protocol of its own.

## Consequences

- PresTag's image changes (about 1 KB of code), so it joins the batched
  firmware qualification. No other image changes.
- Measured on the bench (2026-11-09,
  [power results](../../embedded/tags/families/PresTag/design/power-results.md),
  on the PresTagHR build that preceded this one): low-power conversion costs
  what PresTag does per sample, and low noise about 8 uC more. Low noise
  roughly halves sample-to-sample noise (1.85 Pa measured; the sensor's
  unfiltered figure is 1.8 Pa, against 5.6 Pa in low-current mode), and only
  there does the low pressure byte matter: the sensor's noise then equals the
  1/16 hPa rounding. At 20 s, low noise on a 5.5 mAh cell has no margin for
  five months; an 11 mAh cell or a longer period does.
- Committing generated sources showed that `default_config.c` did not depend
  on the descriptor file, so renumbering a field regenerated the nanopb headers
  and left the default configuration encoding the old number. The rule now
  depends on the file.
- The qtmonitor configuration-probe goldens were patched, not regenerated, for
  the 18 cases that start from the PresTag default; the legacy Qt monitor has
  no pressure-sensor panel.
