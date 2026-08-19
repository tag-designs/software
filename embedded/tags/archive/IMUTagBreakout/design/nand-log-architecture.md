# IMUTag NAND Log Architecture Sketch

Superseded note: the active page-format design is
`embedded/tags/IMUTag/design/log-format.md`. That newer design stores one
timestamped 2048-byte page containing 15 ten-sample superframes, with pressure
and magnetometer values as floats. The older 128-byte-record packing described
below is retained as historical context only.

This note sketches a possible IMUTagBreakout log redesign around large NAND
flash, such as 1 Gbit or 2 Gbit parts with 2 KiB pages and 64 pages per erase
block. It is not an implementation plan yet; it records the data architecture
constraints and the preferred split between the logging layer and the flash
translation layer.

## NAND Geometry

Assumed NAND geometry:

| Property | Value |
| --- | ---: |
| Page size | 2048 bytes |
| Pages per erase block | 64 |
| Erase block size | 128 KiB |
| Maximum bad blocks | 20 |

Current IMUTag raw records are 128 bytes, so the geometry lines up cleanly:

| Unit | Capacity |
| --- | ---: |
| 1 NAND page | 16 IMUTag records |
| 8 NAND pages | 128 IMUTag records |
| 1 NAND erase block | 1024 IMUTag records |

Pages can only be programmed once after erase. Firmware therefore needs to
accumulate a full 2 KiB page before programming NAND. A partial final page can
be padded with erased bytes; the existing per-record footer flags let the host
skip unwritten 128-byte records.

## Layering

The NAND storage stack should have three distinct layers:

1. IMUTag logger
2. Logical page stream
3. Physical NAND driver with bad-block mapping

The logger should write and read logical pages only. It should not know which
physical NAND block stores a given logical page.

The NAND layer owns:

- scanning factory bad-block markers;
- building a small bad-block table;
- mapping logical pages to good physical pages;
- skipping bad blocks during erase, program, and read;
- never erasing or programming a block marked bad.

With this split, a bad physical block is hidden below the logger:

```text
logical block 0 -> physical block 0
logical block 1 -> physical block 1
logical block 2 -> physical block 3  // physical block 2 is bad
```

The logger still sees a contiguous logical page stream.

## Page Stream Interface

The flash abstraction should expose page-sized operations to the logger:

```c
bool flash_stream_append_page(const uint8_t page[2048]);
bool flash_stream_read_page(uint32_t logical_page, uint8_t page[2048]);
bool flash_stream_erase_logical_range(uint32_t first_page, uint32_t page_count);
```

The append path should reject writes that are not exactly one page. The logger
keeps a 2 KiB page buffer, appends sixteen 128-byte records into that buffer,
and programs one NAND page at a time.

## Header Cadence

Internal headers should describe the logical stream, not the physical NAND
placement. If each header covers a strict fixed number of logical NAND pages,
the first page for a header is computed from the header index:

```text
first_logical_page = header_index * PAGES_PER_HEADER
```

Because of that fixed allocation, the header does not need to store:

- physical block number;
- logical block number;
- first logical page;
- sequence number.

The header also does not need a `pages_written` field. On restart, detach, or
reattach, firmware should generate a new header and leave any unwritten pages in
the previous fixed span erased. Restart is expected to be rare, and the existing
record footer flags already let the host skip erased records.

One header per 8 pages is attractive for 1 Gbit NAND:

| IMU ODR | Log blocks/s | Time per 8-page header |
| ---: | ---: | ---: |
| 50 Hz | 6.25 | 20.48 s |
| 100 Hz | 12.5 | 10.24 s |
| 200 Hz | 25 | 5.12 s |
| 400 Hz | 50 | 2.56 s |
| 800 Hz | 100 | 1.28 s |
| 1600 Hz | 200 | 0.64 s |

This cadence gives frequent absolute time anchors without needing an internal
header per NAND page.

## Header Format

Headers must be a multiple of 8 bytes. NAND data and internal headers are
assumed to be ECC-protected, so the header does not include a CRC.

Use the same 8-byte shape as the current IMUTag header:

```c
typedef struct {
    int32_t epoch;
    uint16_t millis_flags;
    int16_t rawtemp;
} t_ImuTagNandLogHeader;
```

Suggested flag behavior:

- erased header: all `0xff`, invalid;
- valid header: `epoch != -1`;
- resync/storage-skip flags reuse the existing packed `millis_flags` model.

If collection restarts before a fixed page span is full, firmware writes the
next header and advances to the next fixed logical page span. It does not need
to update the old header.

## Header Storage Budget

For one 8-byte header every 8 NAND pages:

| NAND size | Logical pages | Headers | Header space |
| ---: | ---: | ---: | ---: |
| 1 Gbit / 128 MiB | 65,536 | 8,192 | 64 KiB |
| 2 Gbit / 256 MiB | 131,072 | 16,384 | 128 KiB |

With an internal header budget around 200 KiB, one header every 8 NAND pages
fits both 1 Gbit and 2 Gbit NAND when the header is 8 bytes.

## Restore Behavior

Restore should be mostly header-driven:

```text
for each valid internal header i:
    first_page = i * PAGES_PER_HEADER
    pages = PAGES_PER_HEADER
```

On restart, firmware should allocate the next header and skip any remaining
unwritten pages in the previous fixed span. Restore therefore does not need to
infer a partial page count from internal metadata. If it needs to determine the
next append point after power loss, it can scan the current fixed span for the
first erased page; the scan is bounded by `PAGES_PER_HEADER`.

The bad-block map is reconstructed separately by the NAND layer from bad-block
markers, or loaded from a small cache if one is added later. The logger should
not persist physical placement in its headers.

## Download Behavior

The monitor/download path can stream logical pages:

1. Read the internal header for the requested header index.
2. Compute `first_logical_page = header_index * PAGES_PER_HEADER`.
3. Read `PAGES_PER_HEADER` logical pages through the flash abstraction.
4. Send one or more raw page payloads to the host.
5. Host splits each 2 KiB page into sixteen 128-byte IMUTag records.
6. Host skips records whose footer flags still mark them as unwritten.

This keeps the host data model close to the current raw block decoder while
changing the transport unit from 128-byte external-flash blocks to 2 KiB NAND
pages.

## Open Questions

- Whether final partial pages should be flushed on normal stop or left erased
  when a new header is generated.
- Whether the raw protobuf payload should carry exactly one NAND page or bundle
  several pages per ACK for throughput.
- Whether an optional debug command should expose the physical bad-block table
  even though normal logging hides it.
