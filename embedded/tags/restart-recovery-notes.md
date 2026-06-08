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

Approximate header counts, assuming an 8-byte `t_DataHeader`, are:

| Available internal flash after `vddHeader` | Header count |
| ---: | ---: |
| 2 KiB | 256 |
| 4 KiB | 512 |
| 8 KiB | 1024 |
| 16 KiB | 2048 |

`BitTag` uses a 16-byte internal data header, so those counts are halved for its
internal-only log.

## IMUTag Timing Caveat

IMUTag FIFO reads are especially sensitive. A reset can leave the hardware FIFO
phase, the local partial block cache, and the saved block timestamp out of sync.
The safest recovery is likely to reset/reinitialize the IMU FIFO stream, discard
a fresh lock/warmup interval, and start the next header from the first
post-resync block timestamp.
