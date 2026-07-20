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
skip factory-bad blocks. Erase must use the same mapping so logical erase block
0, 1, 2, ... erase only known-good physical blocks.

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
must clear state markers and sparse log headers without destroying the bad-block
map. Place it in a dedicated internal-flash section immediately before
`.persistent`.

This placement lets the linker consume whatever flash remains after code,
calibration, and the NAND map. It does not require separate absolute addresses
for 128 KiB versus 256 KiB STM32 variants, because `.persistent` still extends to
the end of `flash0`.

Example linker layout:

```ld
.calibration (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  *(.calibration)
} > flash0

.nand_map (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  __nand_map_start__ = .;
  KEEP(*(.nand_map))
  . = ALIGN(FLASH_PAGE_SIZE);
  __nand_map_end__ = .;
} > flash0

.persistent (NOLOAD): ALIGN(FLASH_PAGE_SIZE)
{
  __persistent_start__ = .;
  *(.persistent)
  . = ALIGN(FLASH_PAGE_SIZE);
  . = ORIGIN(flash0) + LENGTH(flash0);
  __persistent_end__ = .;
} > flash0
```

The current linker scripts use explicit STM32 erase-page alignment values rather
than a shared `FLASH_PAGE_SIZE` symbol. The implemented scripts should use the
correct page alignment for the target family, for example 2048 bytes on
STM32L432 and 4096 bytes on STM32U375.

`erasePersistent()` starts at `__persistent_start__`, so this layout naturally
protects `.nand_map` during ordinary data erase. Full chip reprogramming during
development may still erase the map; startup must detect that and refuse to log
until the map has been configured again.

## Startup Behavior

An erased map is easy to detect because entry 0 will be `0xffff`. A valid map
can never contain `0xffff` for the supported 1-2 Gbit NAND geometries.

Startup should:

1. Reset the NAND and verify its ID.
2. Check `nandLogicalBlockMap[0]`.
3. Refuse to start logging if entry 0 is `0xffff`.
4. Validate the existing map before logging.
5. Refuse to start logging if no valid map is available.

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

## Runtime Rules

- The logger reads and writes logical NAND pages only.
- The NAND driver owns logical-to-physical mapping.
- Known-bad physical blocks are never erased or programmed.
- Logical erase requests map through the same table used for read and program.
- Program or erase failure should initially abort logging rather than trying to
  retire grown bad blocks and rewrite the map in the field.
- The NAND page format now carries page-level timestamp/recovery metadata, so
  internal STM32 log headers can be sparse checkpoints rather than one header per
  NAND page.

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

External erase should use checkpoints to bound the work:

1. Find the highest valid checkpoint and its `logical_page`.
2. Include any possible pages after that checkpoint up to the next checkpoint
   span, because power loss may have left valid NAND pages without a later
   internal checkpoint.
3. Convert the resulting logical page range to logical erase blocks.
4. Erase those logical blocks through the NAND map, never physical block numbers.

On STM32U3 targets, internal flash programming granularity may still require a
16-byte programmed record. In that case the logical checkpoint remains the 8-byte
structure above, wrapped in a padded `t_InternalDataHeader` for flash writes.
