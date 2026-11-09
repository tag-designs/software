---
type: decision
status: accepted
summary: PresTagHR logs the LPS27's full 24-bit pressure as 5-byte register images, 48 to a 240-byte page, through a new driver function that leaves the qualified LPS27 images untouched; low-noise mode is a stored-configuration option whose sub-message presence tells the host which PresTag variant supports it.
---

# 0047. PresTagHR: full-resolution pressure logging

Date: 2026-11-09

## Context

PresTag keeps the top 16 bits of the LPS27HHW's 24-bit pressure, a step of
1/16 hPa (6.25 Pa, about 0.5 m of altitude). The rounding alone is 1.8 Pa RMS,
probably more than the sensor's own noise. Researchers studying the altitude
at which birds stay while feeding want the resolution, at 20 s for 3-5 months.
The plan was [the PresTagHR proposal](../../embedded/tags/families/PresTag/design/proposals/prestaghr.md).

## Decision

- **A third PresTag family member, `PresTagHR`**, selected by
  `PRESTAG_HR_LOG`, in the pattern PresTagRaw set. Same board, storage,
  headers and state machine.
- **Five-byte samples, the register image.** The sample is `PRESS_OUT_XL`
  through `TEMP_OUT_H` as one burst read returns them: 24-bit pressure,
  16-bit temperature (`t_PresTagHRSample`, `include/prestag_log_format.h`).
  48 x 5 = 240, so the page, the header-to-page pairing, erase and paging are
  PresTag's. 4 MB holds 838,860 samples, 194 days at 20 s.
- **A new message, `PresTagHRLog`**, carrying packed `t_PresTagHRBlock`s
  (`Ack` field 18), not a reuse of `PresTagRawLog`: an older host would
  otherwise decode the blocks as 4-byte samples without error.
- **A failed read keeps its slot** as pressure 0x800000, the 24-bit minimum.
  The host writes no row for it, so it shows as a gap, not as -2048 hPa.
- **A new driver function, `lps27GetPressureTempFull()`**, beside
  `lps27GetPressureTemp()`, which is unchanged. PresTag, PresTagRaw, BitPresTag
  and BitPresTagMX25R disassemble identically to before, so none needs
  requalifying. Folding the old function into a truncating wrapper is
  bit-identical in output but changes the sensor transaction in four qualified
  images; it can ride a batched qualification if it is wanted.
- **Low-noise mode is a stored-configuration option**: `Config.lps27`
  (`Lps27.low_noise`, field 11; field 10 stays reserved for
  [`config_schema`](../shared/design/proposals/tag-reported-config-presence.md)),
  stored in `t_storedconfig.lps_low_noise` in what was padding, stored-config
  layout 3. Other tags' nanopb options drop the field, so their images do not
  change.
- **The host learns which variant supports it from presence.** The three
  PresTag variants share `PRESTAG`, so a per-type shape cannot tell them apart.
  `lps27` is a sub-message, which proto3 reports present or absent, and only
  PresTagHR ever sends it. Tag Monitor shows its pressure-sensor panel only for
  a configuration that carries `lps27`.
- **Capture rebuild is supported from the first release**: decoder
  `prestag_hr` in `capturesource.cc`.

## Rejected

- **Six-byte samples** would only pad the 16-bit temperature, and cut 20 s
  capacity to 162 days, too close to five months.
- **Low-noise as a build option**: researchers choose per deployment, which is
  what a stored configuration is for.
- **The sensor's low-pass filter (oversampling)**: it averages successive
  conversions, so each logged sample would cost several conversions instead
  of one one-shot. Low-noise mode is the noise option (user, 2026-11-09).
  A deployment that needs lower noise than that should use UIUCTag, whose
  BMP585 is markedly quieter, with a collection protocol of its own.
- **A new tag type** to separate PresTagHR in the config shape: every switch on
  `TagType` in the host would need it, for one field.

## Consequences

- Measured on the bench (2026-11-09,
  [power results](../../embedded/tags/families/PresTag/design/power-results.md)):
  low-power mode costs what PresTag does per sample, and low-noise mode about
  8 uC more. Low-noise roughly halves sample-to-sample noise (1.85 Pa
  measured; the sensor's unfiltered figure is 1.8 Pa, against 5.6 Pa in
  low-current mode), and only there does the low pressure byte matter: the
  sensor's noise then equals PresTag's 1/16 hPa rounding. At 20 s, low-noise on a 5.5 mAh cell has
  no margin for five months; an 11 mAh cell or a longer period does.
- Committing generated sources showed that `default_config.c` did not depend
  on the descriptor file, so renumbering a field regenerated the nanopb headers
  and left the default configuration encoding the old number. The rule now
  depends on the file.
- The legacy Qt monitor's panel table has no pressure-sensor panel; its
  `--check-config-panels` reports the `PRESTAG` shape's `lps27`.
