# IMUTag Internal Header Checkpoints

## Purpose

IMUTag variants keep a small append-only header log in STM32 internal flash.
For NAND-backed tags this log is not a per-page data table. Instead, it is a
sparse recovery index that helps the firmware locate the next external flash
page after reset, attach recovery, or interrupted collection.

The external flash page remains the authoritative per-page data record. Each
external page starts with the normal `t_DataHeader`; the internal header log is
only a checkpoint stream.

## STM32U3 Header Layout

The current IMUTag page header is shared by the external NAND page image and
the internal checkpoint stream:

```c
typedef struct {
  int32_t epoch;
  uint16_t millis;
  int16_t rawtemp;
} t_ImuTagPageHeader;

typedef t_ImuTagPageHeader t_ImuTagDataHeader;
typedef t_ImuTagDataHeader t_DataHeader;
```

This header is 8 bytes:

- `epoch`: RTC Unix seconds for the start of the page.
- `millis`: low ten bits are 1/1024-second subsecond ticks; upper bits carry
  IMUTag header flags such as resync markers.
- `rawtemp`: pressure-sensor temperature in the native 0.01 C/LSB unit.

For external NAND pages, `millis` remains useful timing metadata. For internal
checkpoint headers, page timing is less important because each external page
contains its own timestamp. The upper flag bits in `millis` should therefore
also carry internal checkpoint events. In particular, define a restart/recovery
flag for the next regular checkpoint written after firmware had to recover the
write cursor from NAND page contents.

STM32U3 flash programming uses 16-byte rows, while the IMUTag data header is 8
bytes. The extra bytes should be used as locator fields:

```c
typedef struct {
  t_DataHeader header;
  uint32_t external_page_logical_next;
  uint32_t external_page_physical_next;
} t_InternalDataHeader;
```

Every internal header is self-contained and headers are written sequentially.
No CRC or extra validity field is needed for the current design because STM32
internal flash has ECC. A header is valid when the STM32 flash read succeeds
and the embedded `t_DataHeader` is not erased.

The locator fields describe the first external page covered by the checkpoint
group:

- `external_page_logical_next`: logical NAND page about to be written.
- `external_page_physical_next`: physical NAND page mapped for that logical
  page when the checkpoint was written.

The physical page is primarily a bring-up and sanity-check breadcrumb. The
logical page remains the normal write and download cursor because the NAND map
owns logical-to-physical translation.

## Cadence

Write one internal checkpoint per 8 external pages.

This cadence is a deliberate storage tradeoff:

- 2 Gbit NAND has 131,072 2 KiB pages.
- One 16-byte internal header per 8 pages requires 16,384 headers.
- 16,384 headers require 262,144 bytes of STM32 internal flash.

That means 256 KiB of internal flash can index a full 2 Gbit NAND device, which
keeps the design compatible with STM32U375 variants that have only 512 KiB of
internal flash.

A cadence of 8 pages also divides a 64-page NAND block evenly, giving eight
checkpoint groups per block. Checkpoint group starts should stay aligned to
logical page multiples of 8 so a group remains within one logical NAND block.

## Page Lookup

The sparse header log is also the index used when the host requests a specific
external page during download.

To locate a requested logical page:

1. Find the newest internal checkpoint whose `external_page_logical_next` is
   less than or equal to the requested page.
2. Confirm the requested page is covered by that checkpoint group:

   ```text
   delta = requested_logical_page - external_page_logical_next
   0 <= delta < 8
   ```

3. Convert directly to a physical NAND page:

   ```text
   physical_page = external_page_physical_next + delta
   ```

This direct mapping works because the 8-page checkpoint group is always inside
one 64-page NAND block. Logical-to-physical mapping happens at block granularity,
so all pages covered by one checkpoint are physically contiguous with the same
page offset. The header therefore gives both the host-facing logical anchor and
the physical anchor needed for efficient page reads.

If a requested page is not covered by the nearest checkpoint, the downloader
should advance to the next checkpoint or return a missing-page result. It should
not infer data for pages outside the checkpoint's 8-page range.

Recovery policy must not be confused with readback policy. When reset or power
loss interrupts an 8-page group, some pages in that group may already have been
written successfully. Download code should treat such a group as partial, not
empty:

- Pages covered by the checkpoint should be read individually when requested.
- A page with a valid external `t_DataHeader` and readable NAND contents should
  be returned to the host.
- An erased, unreadable, or invalid page should be reported as missing or bad.
- Recovery should resume writing at the first erased page found inside the
  checkpoint group.

This preserves any valid data that reached NAND while keeping the internal
checkpoint index sparse and regular.

## Write Ordering

The internal checkpoint is a pre-commit group marker.

At each cadence boundary:

1. Write the internal checkpoint for external pages `N..N+7`.
2. Write the external NAND pages in that group.
3. Do not write another internal checkpoint until page `N+8`.

This ordering means the most recent checkpoint may cover pages that were not
fully written before a reset or power loss. Recovery handles that by scanning
inside the checkpoint group for the first erased page.

Internal checkpoints remain strictly cadence-aligned. Recovery must not write a
mid-group checkpoint. Instead, if recovery occurred while completing group
`N..N+7`, the checkpoint at `N+8` carries the restart/recovery flag.

## Recovery

Recovery should be conservative and simple:

1. Scan internal headers sequentially.
2. Use the last valid header as the latest checkpoint.
3. Treat `external_page_logical_next` as the start of that checkpoint group.
4. Scan pages in that group until the first erased, unreadable, or invalid page.
5. Resume writing at the first erased page if one is found.
6. If all 8 pages in the group are valid, resume at the next cadence boundary.

The NAND page is the authoritative unit of completion: a page write either
happened or did not. Each page carries its own timestamp, so recovery can safely
preserve the already-written prefix of a partially completed group.

After recovery, keep using the normal cadence. Do not add a checkpoint at the
mid-group resume page. When the next cadence boundary is reached, write the
regular checkpoint and set the restart/recovery flag in its internal header.
That flag records that recovery happened before this checkpoint without making
the sparse index irregular.

## Host Implications

Host download code should not assume one internal header per external page.
The download cursor is the recovered external page count, and per-page timing
comes from the `t_DataHeader` stored at the start of each external page.

Internal headers are sparse indexes and recovery breadcrumbs only. A
restart/recovery flag on a checkpoint tells the host that the preceding group
may have been completed after a reset and should be treated as a recovery
boundary for diagnostics.
