# IMUTag NAND Flash Management

The new IMUTag generation uses SPI NAND flash in the 1-2 Gbit range. The
assumed geometry is 2048-byte pages with 64 pages per erase block:

| Density | Physical blocks | Physical pages |
| ---: | ---: | ---: |
| 1 Gbit | 1024 | 65,536 |
| 2 Gbit | 2048 | 131,072 |

Some NAND blocks are marked bad at manufacturing. The factory bad-block mark is
stored in the spare/OOB area of the first page of the block, per the final NAND
datasheet. Factory-bad blocks must not be erased or programmed.

The logger writes NAND pages sequentially. It should see a contiguous logical
page stream while the storage layer maps logical blocks to physical blocks that
skip factory-bad blocks. Runtime logical erase can use the same mapping, but
the final authority before any `BLOCK_ERASE` is the physical block's factory
bad-block marker.

## Flat Logical Block Map

Use a flat logical-block-to-physical-block table stored in STM32 internal flash:

```c
uint16_t nandLogicalBlockMap[NAND_MAP_MAX_BLOCKS];
```

`NAND_MAP_MAX_BLOCKS` should cover the largest supported NAND part, currently
2048 entries for a 2 Gbit device. A 1 Gbit device uses the prefix that
corresponds to its exposed logical block count. Each entry maps one sequential
logical erase block to one physical erase block.

The table is intentionally just an array of `uint16_t`. It does not need a
checksum because STM32 internal flash is ECC protected. Chip identity and
geometry are checked separately by the storage driver's normal ID and capacity
logic.

By construction, the map is strictly monotonically increasing and skips bad
physical blocks. For example, if physical blocks 2 and 5 are bad:

```text
logical block 0 -> physical block 0
logical block 1 -> physical block 1
logical block 2 -> physical block 3
logical block 3 -> physical block 4
logical block 4 -> physical block 6
...
```

Logical-to-physical page mapping then becomes a constant-time array lookup:

```c
uint32_t logical_block = logical_page >> 6;
uint32_t page_in_block = logical_page & 0x3f;
uint32_t physical_block = nandLogicalBlockMap[logical_block];
uint32_t physical_page = (physical_block << 6) | page_in_block;
```

This spends a few KiB of internal flash to keep the runtime storage path simple:
read, program, erase, restore, and test code all use the same direct mapping.

## Linker Placement

The NAND map must not live in ordinary `.persistent`, because ordinary data erase
must clear state markers and sparse log headers without destroying the
bad-block map. Calibration constants should be protected the same way. On
STM32U375 IMUTag builds, reserve the final two internal-flash pages for
provisioned data: the penultimate page for calibration constants and the final
page for the NAND map.

The recommended high-flash layout is:

```text
... code / rodata ...
.persistent          runtime state markers and sparse VDD/checkpoint headers
.calibration         host-provided calibration constants, one erase page
.nand_map            NAND block map, one erase page, blank on non-NAND targets
end of flash0
```

This keeps the map and calibration page out of ordinary state erase during
testing, and keeps them in separate erase pages so calibration writes do not
erase the NAND map. It also helps normal debug/program cycles preserve
provisioned data, provided the programmer is doing page/sector updates rather
than a mass erase. Manufacturing or recovery tools that intentionally wipe the
whole MCU still need to reprovision both calibration and the NAND map.

`erasePersistent()` and any VDD-header erase path must treat
`__persistent_end__` as the exclusive upper bound. They must not derive the erase
end from `ORIGIN(flash0) + LENGTH(flash0)` after the protected tail has been
introduced. This is especially important on STM32U375, where the flash erase
page is 4096 bytes and the existing VDD/checkpoint area may otherwise erase into
the final reserved pages.

Example linker layout:

```ld
.persistent (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  __persistent_start__ = .;
  *(.persistent)
  . = ALIGN(FLASH_PAGE_SIZE);
  . = ORIGIN(flash0) + LENGTH(flash0) - (2 * FLASH_PAGE_SIZE);
  __persistent_end__ = .;
} > flash0

.calibration (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  __calibration_start__ = .;
  KEEP(*(.calibration))
  . = ORIGIN(flash0) + LENGTH(flash0) - FLASH_PAGE_SIZE;
  . = ALIGN(FLASH_PAGE_SIZE);
  __calibration_end__ = .;
} > flash0

.nand_map (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  __nand_map_start__ = .;
  KEEP(*(.nand_map))
  . = ALIGN(FLASH_PAGE_SIZE);
  . = ORIGIN(flash0) + LENGTH(flash0);
  __nand_map_end__ = .;
} > flash0
```

The current linker scripts use explicit STM32 erase-page alignment values rather
than a shared `FLASH_PAGE_SIZE` symbol. The implemented scripts should use the
correct page alignment for the target family, for example 2048 bytes on
STM32L432 and 4096 bytes on STM32U375.

`erasePersistent()` starts at `__persistent_start__` and stops before
`__persistent_end__`, so this layout naturally protects `.calibration` and
`.nand_map` during ordinary data erase. The linker must fail if `.persistent`
cannot fit before the protected tail; silently shrinking or overlapping the
tail is not allowed.

## Startup Behavior

An erased map is easy to detect because entry 0 will be `0xffff`. A valid map
can never contain `0xffff` for the supported 1-2 Gbit NAND geometries.

Storage startup should:

1. Reset the NAND and verify its ID.
2. Check `nandLogicalBlockMap[0]`.
3. Refuse storage readiness if entry 0 is `0xffff`.
4. Validate the existing map before logging.
5. Refuse storage readiness if no valid map is available.

Tag collection startup must gate on that storage readiness the same way it gates
on calibration availability. The tag may still boot far enough for monitor
commands, diagnostics, provisioning, and calibration, but it must not enter the
logging/run state when either of these provisioned records is absent:

- at least one valid calibration constants record;
- a configured and validated NAND logical block map.

The validation pass can remain simple:

```c
for (uint32_t i = 0; i < logical_block_count; i++) {
  uint16_t physical = nandLogicalBlockMap[i];

  if (physical >= physical_block_count)
    fail();

  if (i > 0 && physical <= nandLogicalBlockMap[i - 1U])
    fail();
}
```

When building the map, the scanner walks physical blocks in order, reads each
factory bad-block marker, and appends only good physical blocks to the logical
map until the exposed logical block count is reached.

## Map Configuration

Configuring the NAND map should be an explicit provisioning or self-test action,
not hidden inside the normal logging startup path. This keeps ordinary startup
fast and predictable and avoids unexpectedly writing STM32 flash because a
logger attempted to start.

The external flash test path is a reasonable place to configure the table,
especially during development and manufacturing bring-up. A NAND-specific test
can:

1. Reset the NAND and verify its ID.
2. If `nandLogicalBlockMap[0] == 0xffff`, erase the `.nand_map` STM32 flash
   page(s), scan factory bad-block markers, and program the logical block map.
3. Validate the map.
4. Report the physical bad-block count and exposed logical capacity.
5. Optionally erase, program, read, compare, and erase one reserved logical test
   block.

Normal `tagStorageCheckID()` may validate that an existing map is present and
usable, but it should not silently create the map unless the caller is an
explicit test/provisioning command. That separation makes the failure mode clear:
an unconfigured tag passes neither storage readiness nor logging start until the
map has been provisioned.

## Sensor Data Write Rules

The logger writes sensor data through the NAND page-program cache. It does not
write partially completed sample groups directly to the NAND array. The storage
driver exposes this as the three-step NAND program sequence:

1. `PROGRAM_LOAD` (`0x02`) seeds the chip cache for a new logical page.
2. `PROGRAM_LOAD_RANDOM` (`0x84`) writes later chunks into the same active
   logical page at their page columns.
3. `PROGRAM_EXECUTE` (`0x10`) commits the full cache to the NAND array once the
   2048-byte page is complete.

Rules for the logger and storage layer:

- The first write to an empty page cache must use `PROGRAM_LOAD`.
- Every subsequent write to that same logical page must use
  `PROGRAM_LOAD_RANDOM`.
- Cache writes must not cross a 2048-byte NAND page boundary.
- `PROGRAM_EXECUTE` is issued only after the page-format layer has filled the
  page, including its page header and all sensor superframes.
- Once `PROGRAM_EXECUTE` succeeds, the logical page is immutable until its erase
  block is erased.
- If any cache load or execute fails, abort logging rather than trying to patch
  the page in place.
- A reset before `PROGRAM_EXECUTE` means the active cache contents were never
  committed. Recovery advances to the next safe logical page or block according
  to the checkpoint rules.

The legacy full-page write path may remain for tests and simple utilities, but
normal IMUTag sensor logging should use cache-load operations so sample data can
be accumulated in the NAND cache and committed only at the page boundary.

## Runtime Rules

- The logger reads and writes logical NAND pages only.
- The NAND driver owns logical-to-physical mapping.
- Known-bad physical blocks are never erased or programmed.
- Program paths map logical pages through the NAND map before executing a page.
- Erase paths must verify the physical block's bad-block marker before issuing
  `BLOCK_ERASE`.
- Program or erase failure should initially abort logging rather than trying to
  retire grown bad blocks and rewrite the map in the field.
- The NAND page format now carries page-level timestamp/recovery metadata, so
  internal STM32 log headers can be sparse checkpoints rather than one header per
  NAND page.

## Erase Safety

Erasing a factory-bad block can destroy the only reliable factory bad-block
marker. Because that is unrecoverable, every physical erase must first read the
bad-block marker from the spare/OOB area of the first page of the physical
block. If the marker is not erased (`0xff`), or if it cannot be read, the block
must not be erased.

Bulk erase, external-flash test erase, and provisioning support can ignore the
logical map altogether:

```text
for each physical block:
  read factory bad-block marker
  if marker == 0xff:
    erase physical block
  else:
    skip physical block
```

Logical erase requests may still use the map to identify the physical block for
a logical erase block, but the final guard before `BLOCK_ERASE` is always the
physical marker check. This means erase remains safe even if the map is absent,
stale, or not trusted yet.

## Sparse VDD Header

Because the NAND page format carries page-level timestamp and recovery metadata,
the internal VDD header no longer needs to be written once per NAND page. It can
be a sparse checkpoint table used to find restart locations and determine which
logical erase blocks need to be erased.

The checkpoint does not need subsecond ticks. Each NAND page already stores
`seconds` plus 1/1024 s subsecond ticks in its page header. The internal
checkpoint only needs enough information to locate a durable point in the
logical page stream and mark discontinuities.

One compact 8-byte checkpoint format is:

```c
typedef struct {
  int32_t seconds;
  uint32_t logical_page_flags;
} t_ImuTagNandCheckpoint;
```

`logical_page_flags` is a packed field:

```c
#define IMUTAG_CHECKPOINT_PAGE_MASK   0x00ffffffu
#define IMUTAG_CHECKPOINT_FLAGS_MASK  0xff000000u
#define IMUTAG_CHECKPOINT_FLAG_RESYNC 0x01000000u

uint32_t logical_page = checkpoint.logical_page_flags &
                        IMUTAG_CHECKPOINT_PAGE_MASK;
uint32_t flags = checkpoint.logical_page_flags &
                 IMUTAG_CHECKPOINT_FLAGS_MASK;
```

The low 24 bits allow over 16 million logical pages, far more than the 131,072
physical pages in a 2 Gbit NAND with 2048-byte pages. The upper 8 bits are
available for checkpoint flags. Initial flags should include at least:

- `RESYNC`: collection restarted after a reset, monitor attach, clock/FIFO
  restart, or other discontinuity;
- `STORAGE_SKIP`: firmware intentionally skipped one or more logical pages,
  usually to abandon a partial page after reset;
- `SEGMENT_START`: optional marker for the first checkpoint of a new run or
  resumed collection segment.

`seconds` should be copied from the NAND page header for `logical_page`. Exact
sample timing and subsecond phase are recovered from the page itself, not from
the internal checkpoint.

An erased checkpoint has both words erased, so `seconds == -1` and
`logical_page_flags == 0xffffffff`. A valid checkpoint must have a logical page
within the exposed logical capacity and only known flag bits set.

Checkpoint cadence should be sparse and fixed. Good candidates are:

- one checkpoint per logical erase block, so each checkpoint naturally names the
  first page of a block;
- one checkpoint every N logical pages, if faster restart recovery is worth the
  extra STM32 flash writes.

For a one-checkpoint-per-block policy:

```c
logical_page = logical_block * NAND_PAGES_PER_BLOCK;
logical_block = logical_page >> 6;
```

When a new checkpoint span begins, firmware should program the checkpoint before
the first NAND page in that span is committed to the array. A good sequence is:

1. Load the new NAND page header and first superframe into the NAND program
   cache.
2. Program the internal checkpoint for that logical page.
3. Execute the NAND page program.

If checkpoint programming fails, firmware should abandon the page and stop or
finish the run rather than committing data that erase/restart recovery may not be
able to find later.

This makes erase planning simple. After restore, the firmware or monitor can
read the final valid checkpoint, recover the last known logical page, round up to
the next logical erase block if needed, and erase only the blocks known to have
held data.

Restore should be:

1. Scan the internal checkpoint table until the first erased record.
2. Use the last valid checkpoint as the lower bound for the last written region.
3. Read NAND pages forward from `logical_page`, using each page's own header to
   find the first erased or invalid page.
4. Resume at that next erased logical page, or start a new segment and set
   `RESYNC`/`STORAGE_SKIP` if a partial page or partial span is abandoned.

Runtime log erase should use checkpoints to bound the work:

1. Find the highest valid checkpoint and its `logical_page`.
2. Include any possible pages after that checkpoint up to the next checkpoint
   span, because power loss may have left valid NAND pages without a later
   internal checkpoint.
3. Convert the resulting logical page range to logical erase blocks.
4. Map each logical block to its physical block, read the physical block's
   bad-block marker, and issue `BLOCK_ERASE` only if the marker is erased.

Bulk/test erase is a separate operation: it may scan physical blocks directly
and erase only blocks whose factory bad-block marker is still erased.

On STM32U3 targets, internal flash programming granularity may still require a
16-byte programmed record. In that case the logical checkpoint remains the 8-byte
structure above, wrapped in a padded `t_InternalDataHeader` for flash writes.

## Implementation Work Plan

1. Update linker placement.
   - Done for STM32U375: `.calibration` is in the penultimate flash page and
     `.nand_map` is in the final flash page.
   - Done for STM32U375: `.persistent` ends before the protected
     calibration/map tail.
   - Done for STM32U375: `__calibration_*`, `__nand_map_*`, and
     `__persistent_*` symbols are available for erase/provisioning bounds.

2. Harden internal-flash erase and calibration writes.
   - Done: keep `erasePersistent()` bounded by `__persistent_start__` and
     `__persistent_end__`.
   - Done: guard IMUTag VDD/header writes before programming beyond
     `__persistent_end__`.
   - Done: update IMUTag calibration erase to use address-based erase on
     STM32U375 instead of fixed 2048-byte L432 page math.

3. Enforce startup readiness.
   - Make NAND `check_id` fail when the logical map is erased or invalid.
   - Gate IMUTag collection entry on both `sensorsHaveCalibration()` and NAND
     readiness.
   - Keep monitor/provisioning commands available so an unprovisioned tag can be
     recovered without entering logging.

4. Implement sensor data cache programming.
   - Route normal IMUTag sensor writes through program-cache load operations.
   - Use `PROGRAM_LOAD` for the first chunk of a logical page and
     `PROGRAM_LOAD_RANDOM` for subsequent chunks.
   - Execute the program cache only when the page-format layer has filled the
     page.
   - Add tests or instrumentation for page-boundary behavior, reset before
     execute, and execute failure.

5. Make erase bad-block safe.
   - Add a physical-marker check immediately before every NAND block erase.
   - Allow bulk/test erase to scan physical blocks directly and skip marked bad
     blocks without consulting the map.
   - Treat unreadable markers as unsafe and skip/fail the erase request.
   - Report skipped bad blocks in external flash test/provisioning diagnostics.

6. Verify end to end.
   - Build the IMUTag NAND target.
   - Run external flash provisioning/test on hardware and confirm the reported
     bad-block count.
   - Confirm ordinary persistent erase clears VDD/checkpoint records but
     preserves calibration constants and the NAND map.
   - Confirm collection refuses to start before calibration or map provisioning
     and starts after both are present.
