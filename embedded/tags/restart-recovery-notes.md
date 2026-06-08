# Restart Recovery Notes

These notes capture deferred design thinking for tag recovery after monitor
connect-under-reset or another reset during active acquisition. They are not an
implemented behavior contract yet.

## Current Direction

- Treat `pState` as a retained recovery journal because it lives in RTC backup
  registers and survives MCU reset.
- Reinitialize software ownership on every boot: bus semaphores, GPIO muxes,
  EXTI routing, trigger timers, and peripheral register drivers.
- Add retained acquisition-phase sentinels around critical sections such as
  sensor read, header write, external data write, and cursor commit.
- If reset occurs during sensor read or data logging, prefer abandoning the
  current block/page over trying to reconstruct partially read or partially
  written data.

## Header/Page Recovery

One simple recovery policy is to start a new `vddHeader` after reset and
abandon unused external pages under the previous header. This avoids exposing a
partially populated page, but log-download timing code must understand that
state markers can indicate a restart and a discontinuity between headers.

The download path should eventually combine:

- `vddHeader` timestamps for ordinary page anchors;
- state markers for restart/discontinuity events;
- retained cursor/sentinel state to decide whether the final pre-reset page is
  complete, abandoned, or should be hidden.

## Header Validation and ECC

Internal flash headers and state markers should be read through the checked
helpers in `stm32flash.c`, not by direct struct access. Those helpers install a
narrow `NMI_Handler` path that converts flash ECC NMIs into read errors only
while an explicit flash probe is active. ECC outside that probe remains an
unexpected exception.

Recovery scanners should treat checked-read failure the same way they treat an
erased or invalid header boundary: stop before that record and abandon the
possibly incomplete page. Download code can additionally defer exposing a page
until either the following header exists or a terminal state marker proves that
the final page was completed.

A useful hardware test is to run a tag, interrupt/reset it repeatedly during
internal header writes, then verify that recovery and monitor download stop at
the last checked-readable header instead of entering the generic exception path.
If deliberately producing an ECC-faulted double-word is practical on a bench
unit, the expected result is that a guarded read returns an ECC error and an
unguarded read still follows the ordinary exception path.

## Storage Bounds

The monitor download path should treat the internal flash space from
`vddHeader` to flash end as the first practical limit. The persistent section
intentionally lives at the trailing end of flash, and existing recovery/download
code treats `vddHeader` as the start of a flash-backed trailing header table,
not as a fixed-size C array boundary. The linker expands `.persistent` to the
end of `flash0` and exports `__persistent_end__`, so firmware can use that
symbol instead of reading the MCU flash-size register at runtime. The effective
header limit is therefore
approximately:

```c
((uint32_t)&__persistent_end__ - (uint32_t)&vddHeader[0]) /
    sizeof(t_DataHeader)
```

That limit depends on the final linked image size and is best checked from the
map file by comparing `&vddHeader[0]` to `&__persistent_end__`.

Approximate active-target external flash capacities:

| Target | External flash | Capacity | Notes |
| --- | --- | ---: | --- |
| `BitTag` | none | n/a | Internal flash log only. |
| `PresTag` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `BitPresTag` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `BitPresTagMX25R` | MX25R | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTag` | MX25R | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTagAT25` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTagAT25Breakout` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `IMUTagBreakout` | MX25L | 16 MiB | Capacity comes from `EXT_FLASH_SIZE`. |

External-flash capacity is usually not the limiting factor for current log
download. For example, 4 MiB flash can hold many thousands of current PresTag,
BitPresTag, or CompassTag log pages, and IMUTagBreakout can hold 8192 current
2048-byte pages. The internal trailing header space normally limits complete
anchored downloads before external flash fills.

The current number of complete internal-header pages needed to fill external
flash is:

| Target | Bytes written per internal header | External flash | Complete headers to fill | Unused tail bytes | First header that cannot fit |
| --- | ---: | ---: | ---: | ---: | ---: |
| `PresTag` | 240 B | 4 MiB | 17,476 | 64 | 17,477 |
| `BitPresTag` | 96 B | 4 MiB | 43,690 | 64 | 43,691 |
| `BitPresTagMX25R` | 96 B | 4 MiB | 43,690 | 64 | 43,691 |
| `CompassTag` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |
| `CompassTagAT25Breakout` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |
| `CompassTagAT25` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |
| `IMUTagBreakout` | 2,048 B | 16 MiB | 8,192 | 0 | 8,193 |

`BitTag` has no external flash. For 256 KiB internal flash builds, external
flash fills first for `PresTag`, the CompassTag variants, and IMUTagBreakout.
The internal `vddHeader` region fills first for the BitPresTag variants.

Current linked `vddHeader` limits from
`/Users/geobrown/Build/tag-designs/software-embedded-clean`, with
`TAG_FLASH_SIZE=256K`, are:

| Target | `vddHeader` | Header size | `__persistent_end__` | Header limit |
| --- | ---: | ---: | ---: | ---: |
| `PresTag` | `0x08007a60` | 8 B | `0x08040000` | 28,852 |
| `BitPresTag` | `0x08008a60` | 8 B | `0x08040000` | 28,340 |
| `BitPresTagMX25R` | `0x08008a60` | 8 B | `0x08040000` | 28,340 |
| `CompassTag` | `0x08009258` | 8 B | `0x08040000` | 28,085 |
| `CompassTagAT25Breakout` | `0x08009258` | 8 B | `0x08040000` | 28,085 |
| `CompassTagAT25` | `0x08009a58` | 8 B | `0x08040000` | 27,829 |
| `IMUTagBreakout` | `0x0800b1f0` | 8 B | `0x08040000` | 27,074 |
| `BitTag` | `0x08007268` | 16 B | `0x08040000` | 14,553 |

For a 128 KiB build, assuming similar code layout and
`__persistent_end__ = 0x08020000`, the approximate limits would be:

| Target | Approximate 128 KiB header limit |
| --- | ---: |
| `PresTag` | 12,468 |
| `BitPresTag` | 11,956 |
| `BitPresTagMX25R` | 11,956 |
| `CompassTag` | 11,701 |
| `CompassTagAT25Breakout` | 11,701 |
| `CompassTagAT25` | 11,445 |
| `IMUTagBreakout` | 10,690 |
| `BitTag` | 6,361 |

`BitTag` uses a 16-byte internal data header; the other current active targets
listed above use 8-byte `t_DataHeader` records.

## IMUTag Timing Caveat

IMUTag FIFO reads are especially sensitive. A reset can leave the hardware FIFO
phase, the local partial block cache, and the saved block timestamp out of sync.
The safest recovery is likely to reset/reinitialize the IMU FIFO stream, discard
a fresh lock/warmup interval, and start the next header from the first
post-resync block timestamp.

`t_DataHeader.millis` uses only ten bits for the 0..999 millisecond value. The
IMUTag log format now reserves bit `0x0400` as `IMUTAG_HEADER_RESYNC`, which
marks the first header after the FIFO stream has been reinitialized. The host
decoder should treat that header as the start of a new smooth timing segment:
anchor the segment to the header epoch/millisecond, then place samples by IMU
sample count until the next resync marker. Ordinary headers should not
re-anchor the high-rate data because the millisecond field can introduce
page-to-page jitter. If the millisecond-resolution resync anchor would place the
new segment before samples already emitted for the previous segment, the decoder
rounds the new segment start up to the next expected block boundary so elapsed
microsecond timestamps remain monotonic.

SQLite logs retain the decoded header flags in `ImuHeader.Flags` and write a
`RESYNC` row to `ImuEvent` at the corresponding elapsed microsecond time.
SensorViz can draw those event rows as vertical discontinuity markers without
turning them into y-axis streams.
