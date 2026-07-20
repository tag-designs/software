/**
 * @file gd5f.c
 * @brief GD5F SPI-NAND command implementation with bad-block mapping.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#include "hal.h"
#include "debug_log.h"
#include "flash_internal.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_gd5f.h"
#include "storage_spi.h"

#include <string.h>

#define GD5F_CMD_WRITE_ENABLE        0x06U
#define GD5F_CMD_GET_FEATURE         0x0FU
#define GD5F_CMD_SET_FEATURE         0x1FU
#define GD5F_CMD_PAGE_READ           0x13U
#define GD5F_CMD_READ_CACHE          0x03U
#define GD5F_CMD_PROGRAM_LOAD        0x02U
#define GD5F_CMD_PROGRAM_LOAD_RANDOM 0x84U
#define GD5F_CMD_PROGRAM_EXECUTE     0x10U
#define GD5F_CMD_BLOCK_ERASE         0xD8U
#define GD5F_CMD_READ_ID             0x9FU
#define GD5F_CMD_RESET               0xFFU

#define GD5F_FEATURE_BLOCK_LOCK      0xA0U
#define GD5F_FEATURE_STATUS          0xC0U

#define GD5F_STATUS_OIP              0x01U
#define GD5F_STATUS_WEL              0x02U
#define GD5F_STATUS_E_FAIL           0x04U
#define GD5F_STATUS_P_FAIL           0x08U

#ifndef GD5F_ID_MANUFACTURER
#define GD5F_ID_MANUFACTURER         0xC8U
#endif
#ifndef GD5F_ID_DEVICE
#define GD5F_ID_DEVICE               0x41U
#endif

#define GD5F_BAD_BLOCK_MARK_COLUMN   GD5F_PAGE_SIZE
#define GD5F_BAD_BLOCK_MARK          0xFFU

#define GD5F_RESET_DELAY_MS          1U
#define GD5F_READY_POLL_MS           1U
#define GD5F_READ_POLL_LIMIT         4U
#define GD5F_PROGRAM_POLL_LIMIT      8U
#define GD5F_ERASE_POLL_LIMIT        12U

#define GD5F_MAP_PROGRAM_WORDS       4U
#define GD5F_MAP_ENTRIES_PER_ROW \
  ((GD5F_MAP_PROGRAM_WORDS * sizeof(uint32_t)) / sizeof(uint16_t))

uint16_t gd5fLogicalBlockMap[GD5F_PHYSICAL_BLOCK_COUNT]
    __attribute__((section(".nand_map"), used, aligned(16)));

static uint16_t gd5f_provision_map[GD5F_PHYSICAL_BLOCK_COUNT];
static bool gd5f_logical_map_loaded;
static bool gd5f_cache_active;
static uint32_t gd5f_cache_logical_page;

/** @name SPI command primitives
 * These helpers encode GD5F command headers and own chip-select
 * boundaries for single-command transfers. Higher layers pass logical storage
 * requests down to these routines only after address mapping is complete.
 * @{
 */
/**
 * @brief Write raw bytes to the selected SPI storage bus.
 */
static bool gd5fWriteBytes(const TagSpiDevice *spi, const uint8_t *buf,
                           uint32_t len)
{
  return tagStorageSpiWrite(spi, buf, len);
}

/**
 * @brief Send a command whose address operand is a NAND row/page address.
 */
static bool gd5fRowCommand(const TagSpiDevice *spi, uint8_t command,
                           uint32_t row)
{
  uint8_t buf[4];

  buf[0] = command;
  buf[1] = (uint8_t)(row >> 16);
  buf[2] = (uint8_t)(row >> 8);
  buf[3] = (uint8_t)row;
  return tagSpiBusWrite(spi, buf, sizeof(buf));
}

/**
 * @brief Stream bytes from a column in the NAND read cache.
 */
static bool gd5fColumnRead(const TagSpiDevice *spi, uint16_t column,
                           uint8_t *buf, uint32_t len)
{
  uint8_t header[4];
  bool ok;

  header[0] = GD5F_CMD_READ_CACHE;
  header[1] = (uint8_t)(column >> 8);
  header[2] = (uint8_t)column;
  header[3] = 0U;
  tagSpiSelect(spi);
  ok = gd5fWriteBytes(spi, header, sizeof(header)) &&
       tagStorageSpiBlockRead(spi, buf, len);
  tagSpiDeselect(spi);
  return ok;
}

/**
 * @brief Stream bytes into the NAND program cache at a page column.
 */
static bool gd5fColumnWrite(const TagSpiDevice *spi,
                            uint8_t command, uint16_t column,
                            const uint8_t *buf, uint32_t len)
{
  uint8_t header[3];
  bool ok;

  header[0] = command;
  header[1] = (uint8_t)(column >> 8);
  header[2] = (uint8_t)column;
  tagSpiSelect(spi);
  ok = gd5fWriteBytes(spi, header, sizeof(header)) &&
       tagStorageSpiBlockWrite(spi, buf, len);
  tagSpiDeselect(spi);
  return ok;
}

/**
 * @brief Convert a physical page number to the command row address.
 */
static uint32_t gd5fPageToRow(uint32_t physical_page)
{
  return physical_page;
}

/**
 * @brief Convert a physical block number to the first row in that block.
 */
static uint32_t gd5fBlockToRow(uint32_t physical_block)
{
  return physical_block * GD5F_PAGES_PER_BLOCK;
}
/** @} */

/** @name Status, feature, and identity operations
 * This collection handles chip state: feature registers, ready polling, write
 * enable latch verification, and the reset/read-ID probe. Normal readiness also
 * requires a configured logical map, but the raw probe is kept separate so
 * provisioning can run on an erased map.
 * @{
 */
/**
 * @brief Read one GD5F feature register.
 */
static bool gd5fGetFeature(const TagStorageDevice *dev, uint8_t feature,
                           uint8_t *value)
{
  uint8_t cmd[2];
  bool ok;

  cmd[0] = GD5F_CMD_GET_FEATURE;
  cmd[1] = feature;
  tagSpiSelect(tagStorageSpiDevice(dev));
  ok = gd5fWriteBytes(tagStorageSpiDevice(dev), cmd, sizeof(cmd)) &&
       tagStorageSpiBlockRead(tagStorageSpiDevice(dev), value, 1U);
  tagSpiDeselect(tagStorageSpiDevice(dev));
  return ok;
}

/**
 * @brief Write one GD5F feature register.
 */
static bool gd5fSetFeature(const TagStorageDevice *dev, uint8_t feature,
                           uint8_t value)
{
  uint8_t cmd[3];

  cmd[0] = GD5F_CMD_SET_FEATURE;
  cmd[1] = feature;
  cmd[2] = value;
  return tagSpiBusWrite(tagStorageSpiDevice(dev), cmd, sizeof(cmd));
}

/**
 * @brief Poll the status register until the chip is ready or the limit expires.
 */
static bool gd5fWaitReady(const TagStorageDevice *dev, uint32_t poll_limit,
                          uint8_t *status)
{
  uint8_t sr = 0U;

  while (poll_limit-- > 0U) {
    stopMilliseconds(GD5F_READY_POLL_MS);
    if (!gd5fGetFeature(dev, GD5F_FEATURE_STATUS, &sr))
      return false;
    if ((sr & GD5F_STATUS_OIP) == 0U) {
      if (status != NULL)
        *status = sr;
      return true;
    }
  }
  return false;
}

/**
 * @brief Set and verify the write-enable latch before program or erase.
 */
static bool gd5fWriteEnable(const TagStorageDevice *dev)
{
  uint8_t status;

  if (!tagStorageSpiCommand(tagStorageSpiDevice(dev),
                            GD5F_CMD_WRITE_ENABLE))
    return false;
  if (!gd5fGetFeature(dev, GD5F_FEATURE_STATUS, &status))
    return false;
  return (status & GD5F_STATUS_WEL) != 0U;
}

/**
 * @brief Reset the NAND, unlock blocks, and verify manufacturer/device ID.
 */
static int gd5fProbe(const TagStorageDevice *dev)
{
  uint8_t cmd[2] = { GD5F_CMD_READ_ID, 0U };
  uint8_t id[2] = { 0U, 0U };
  bool ok;

  tagStorageSpiCommand(tagStorageSpiDevice(dev), GD5F_CMD_RESET);
  stopMilliseconds(GD5F_RESET_DELAY_MS);
  gd5fSetFeature(dev, GD5F_FEATURE_BLOCK_LOCK, 0U);

  tagSpiSelect(tagStorageSpiDevice(dev));
  ok = gd5fWriteBytes(tagStorageSpiDevice(dev), cmd, sizeof(cmd)) &&
       tagStorageSpiBlockRead(tagStorageSpiDevice(dev), id, sizeof(id));
  tagSpiDeselect(tagStorageSpiDevice(dev));
  if (!ok)
    return -1;

  if (MONCONNECTED)
    debug_log_printf("NAND: MID 0x%x DID 0x%x", id[0], id[1]);

  if (id[0] != GD5F_ID_MANUFACTURER ||
      id[1] != GD5F_ID_DEVICE)
    return -1;

  return 0;
}
/** @} */

/** @name Physical read helpers
 * NAND reads are two-stage operations: transfer a physical array page into the
 * chip cache, then stream a byte range from that cache. These helpers do not
 * perform logical mapping; callers supply physical page addresses.
 * @{
 */
/**
 * @brief Read bytes from one physical NAND page and column.
 */
static bool gd5fReadPhysicalPage(const TagStorageDevice *dev,
                                 uint32_t physical_page, uint16_t column,
                                 uint8_t *buf, uint32_t len)
{
  uint8_t status;

  if (!gd5fRowCommand(tagStorageSpiDevice(dev),
                      GD5F_CMD_PAGE_READ,
                      gd5fPageToRow(physical_page)))
    return false;
  if (!gd5fWaitReady(dev, GD5F_READ_POLL_LIMIT, &status))
    return false;
  return gd5fColumnRead(tagStorageSpiDevice(dev), column, buf, len);
}
/** @} */

/** @name Logical block map and provisioning
 * The runtime address translator is a flat internal-flash array of physical
 * block numbers. Provisioning is the only code path that scans factory
 * bad-block markers; logging and normal reads only validate and consume the
 * prebuilt table.
 * @{
 */
/**
 * @brief Test the factory bad-block marker for one physical block.
 */
static bool gd5fBlockIsGood(const TagStorageDevice *dev,
                            uint32_t physical_block)
{
  uint8_t mark = 0U;
  uint32_t physical_page = physical_block * GD5F_PAGES_PER_BLOCK;

  if (!gd5fReadPhysicalPage(dev, physical_page,
                            GD5F_BAD_BLOCK_MARK_COLUMN,
                            &mark, 1U))
    return false;
  return mark == GD5F_BAD_BLOCK_MARK;
}

/**
 * @brief Return true when the map has been provisioned.
 */
bool gd5fLogicalMapConfigured(void)
{
  return gd5fLogicalBlockMap[0] != GD5F_MAP_ERASED_ENTRY;
}

/**
 * @brief Validate that the provisioned logical map is sorted and in range.
 */
bool gd5fLogicalMapValidate(void)
{
  uint16_t previous_block = 0U;

  if (!gd5fLogicalMapConfigured())
    return false;

  for (uint32_t i = 0U; i < GD5F_LOGICAL_BLOCK_COUNT; i++) {
    uint16_t physical_block = gd5fLogicalBlockMap[i];

    if (physical_block >= GD5F_PHYSICAL_BLOCK_COUNT)
      return false;
    if (i > 0U && previous_block >= physical_block)
      return false;
    previous_block = physical_block;
  }
  return true;
}

/**
 * @brief Lazily validate and remember that the map is usable for this boot.
 */
static bool gd5fEnsureLogicalMap(void)
{
  if (gd5f_logical_map_loaded)
    return true;

  gd5f_logical_map_loaded = gd5fLogicalMapValidate();
  return gd5f_logical_map_loaded;
}

/**
 * @brief Convert a logical page to a physical page through the flat map.
 */
static bool gd5fMapLogicalPage(uint32_t logical_page,
                               uint32_t *physical_page)
{
  uint32_t logical_block = logical_page / GD5F_PAGES_PER_BLOCK;
  uint32_t page_in_block = logical_page % GD5F_PAGES_PER_BLOCK;
  uint16_t physical_block;

  if (logical_block >= GD5F_LOGICAL_BLOCK_COUNT)
    return false;

  physical_block = gd5fLogicalBlockMap[logical_block];
  if (physical_block >= GD5F_PHYSICAL_BLOCK_COUNT)
    return false;

  *physical_page = ((uint32_t)physical_block * GD5F_PAGES_PER_BLOCK) +
                   page_in_block;
  return true;
}

/**
 * @brief Erase and program the internal-flash page holding the flat map.
 */
static bool gd5fProgramLogicalMap(const uint16_t *map)
{
  uint32_t flash_error = 0U;
  uint32_t page_size = FLASH_PageSize();
  uintptr_t start = (uintptr_t)gd5fLogicalBlockMap;
  uintptr_t end = start + sizeof(gd5fLogicalBlockMap);
  uintptr_t erase_start = start - (start % page_size);

  chSysLock();
  FLASH_Unlock();
  for (uintptr_t address = erase_start; address < end; address += page_size) {
    FLASH_PageEraseAddress((uint32_t)address);
  }

  for (uint32_t entry = 0U; entry < GD5F_PHYSICAL_BLOCK_COUNT;
       entry += GD5F_MAP_ENTRIES_PER_ROW) {
    uint32_t row[GD5F_MAP_PROGRAM_WORDS];

    for (uint32_t word = 0U; word < GD5F_MAP_PROGRAM_WORDS; word++) {
      uint32_t first = entry + (word * 2U);
      row[word] = (uint32_t)map[first] |
                  ((uint32_t)map[first + 1U] << 16);
    }

    flash_error = FLASH_Program_Array(
        (uint32_t *)&gd5fLogicalBlockMap[entry], row,
        GD5F_MAP_PROGRAM_WORDS);
    if (flash_error != 0U)
      break;
  }
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

  gd5f_logical_map_loaded = false;
  return flash_error == 0U;
}

/**
 * @brief Build and persist the flat logical map from factory bad-block marks.
 */
bool gd5fProvisionLogicalMap(const TagStorageDevice *dev,
                             uint32_t *bad_block_count)
{
  uint32_t good_count = 0U;
  uint32_t bad_count = 0U;

  if (gd5fProbe(dev) < 0)
    return false;

  memset(gd5f_provision_map, 0xff,
         sizeof(gd5f_provision_map));

  for (uint32_t block = 0U; block < GD5F_PHYSICAL_BLOCK_COUNT; block++) {
    if (gd5fBlockIsGood(dev, block)) {
      if (good_count < GD5F_LOGICAL_BLOCK_COUNT)
        gd5f_provision_map[good_count++] = (uint16_t)block;
    } else {
      bad_count++;
    }
  }

  if (bad_block_count != NULL)
    *bad_block_count = bad_count;
  if (good_count < GD5F_LOGICAL_BLOCK_COUNT)
    return false;

  if (!gd5fProgramLogicalMap(gd5f_provision_map))
    return false;

  gd5f_logical_map_loaded = gd5fLogicalMapValidate();
  if (MONCONNECTED && gd5f_logical_map_loaded)
    debug_log_printf("NAND: provisioned map, bad blocks %u", bad_count);

  return gd5f_logical_map_loaded;
}
/** @} */

/** @name Mapped read operations
 * Public storage reads accept logical byte addresses. This layer slices the
 * request across logical pages, maps each page, and delegates the actual NAND
 * cache read to the physical-page helper.
 * @{
 */
/**
 * @brief Read a byte range from the logical NAND address space.
 */
static void gd5fRead(const TagStorageDevice *dev, uint32_t address,
                     uint8_t *buf, int num)
{
  if (!gd5fEnsureLogicalMap())
    return;

  while (num > 0) {
    uint32_t physical_page;
    uint32_t logical_page = address / GD5F_PAGE_SIZE;
    uint16_t column = (uint16_t)(address % GD5F_PAGE_SIZE);
    uint32_t bytes = GD5F_PAGE_SIZE - column;

    if (bytes > (uint32_t)num)
      bytes = (uint32_t)num;
    if (!gd5fMapLogicalPage(logical_page, &physical_page))
      return;
    if (!gd5fReadPhysicalPage(dev, physical_page, column, buf, bytes))
      return;

    address += bytes;
    buf += bytes;
    num -= (int)bytes;
  }
}
/** @} */

/** @name Program and erase operations
 * NAND page programming is also staged: load bytes into the chip cache with
 * either PROGRAM_LOAD or PROGRAM_LOAD_RANDOM, then commit the cache with
 * PROGRAM_EXECUTE. Erase requests map logical erase blocks to physical blocks.
 * @{
 */
/**
 * @brief Program one complete mapped physical page.
 */
static bool gd5fProgramPhysicalPage(const TagStorageDevice *dev,
                                    uint32_t physical_page,
                                    const uint8_t *buf)
{
  uint8_t status;

  if (!gd5fWriteEnable(dev))
    return false;
  if (!gd5fColumnWrite(tagStorageSpiDevice(dev),
                       GD5F_CMD_PROGRAM_LOAD, 0U,
                       buf, GD5F_PAGE_SIZE))
    return false;
  if (!gd5fWriteEnable(dev))
    return false;
  if (!gd5fRowCommand(tagStorageSpiDevice(dev),
                      GD5F_CMD_PROGRAM_EXECUTE,
                      gd5fPageToRow(physical_page)))
    return false;
  if (!gd5fWaitReady(dev, GD5F_PROGRAM_POLL_LIMIT, &status))
    return false;
  return (status & GD5F_STATUS_P_FAIL) == 0U;
}

/**
 * @brief Program full pages through the legacy storage write callback.
 */
static bool gd5fWrite(const TagStorageDevice *dev, uint32_t address,
                      uint8_t *buf, int *cnt)
{
  int requested = *cnt;
  int written = 0;

  *cnt = 0;
  if (!gd5fEnsureLogicalMap())
    return false;
  if ((address % GD5F_PAGE_SIZE) != 0U ||
      (requested % (int)GD5F_PAGE_SIZE) != 0)
    return false;

  while (requested > 0) {
    uint32_t physical_page;
    uint32_t logical_page = address / GD5F_PAGE_SIZE;

    if (!gd5fMapLogicalPage(logical_page, &physical_page))
      return false;
    if (!gd5fProgramPhysicalPage(dev, physical_page, buf))
      return false;

    address += GD5F_PAGE_SIZE;
    buf += GD5F_PAGE_SIZE;
    requested -= (int)GD5F_PAGE_SIZE;
    written += (int)GD5F_PAGE_SIZE;
    *cnt = written;
  }
  return true;
}

/**
 * @brief Load bytes into the NAND page-program cache with the selected command.
 */
static bool gd5fProgramCacheLoad(const TagStorageDevice *dev,
                                 uint32_t address, const uint8_t *buf,
                                 int cnt, uint8_t command)
{
  uint32_t logical_page = address / GD5F_PAGE_SIZE;
  uint16_t column = (uint16_t)(address % GD5F_PAGE_SIZE);
  bool random = command == GD5F_CMD_PROGRAM_LOAD_RANDOM;

  if (buf == NULL || cnt <= 0)
    return false;
  if (command != GD5F_CMD_PROGRAM_LOAD &&
      command != GD5F_CMD_PROGRAM_LOAD_RANDOM)
    return false;
  if (!gd5fEnsureLogicalMap())
    return false;
  if ((uint32_t)cnt > (GD5F_PAGE_SIZE - column))
    return false;
  if (logical_page >= GD5F_LOGICAL_PAGE_COUNT)
    return false;
  if (random && (!gd5f_cache_active ||
                 gd5f_cache_logical_page != logical_page))
    return false;
  if (!random) {
    gd5f_cache_active = false;
    gd5f_cache_logical_page = logical_page;
  }

  if (!gd5fWriteEnable(dev))
    return false;
  if (!gd5fColumnWrite(tagStorageSpiDevice(dev), command, column, buf,
                       (uint32_t)cnt))
    return false;

  gd5f_cache_active = true;
  return true;
}

/**
 * @brief Seed a new NAND page-program cache using PROGRAM_LOAD (0x02).
 */
static bool gd5fProgramLoad(const TagStorageDevice *dev, uint32_t address,
                            const uint8_t *buf, int cnt)
{
  return gd5fProgramCacheLoad(dev, address, buf, cnt,
                              GD5F_CMD_PROGRAM_LOAD);
}

/**
 * @brief Patch an active NAND program cache using PROGRAM_LOAD_RANDOM (0x84).
 */
static bool gd5fProgramLoadRandom(const TagStorageDevice *dev,
                                  uint32_t address, const uint8_t *buf,
                                  int cnt)
{
  return gd5fProgramCacheLoad(dev, address, buf, cnt,
                              GD5F_CMD_PROGRAM_LOAD_RANDOM);
}

/**
 * @brief Commit the active NAND program cache to the mapped physical page.
 */
static bool gd5fProgramExecuteCache(const TagStorageDevice *dev,
                                    uint32_t address)
{
  uint8_t status;
  uint32_t physical_page;
  uint32_t logical_page = address / GD5F_PAGE_SIZE;
  bool ok;

  if (!gd5fEnsureLogicalMap())
    return false;
  if (!gd5f_cache_active || gd5f_cache_logical_page != logical_page)
    return false;
  if (!gd5fMapLogicalPage(logical_page, &physical_page))
    return false;

  ok = gd5fWriteEnable(dev) &&
       gd5fRowCommand(tagStorageSpiDevice(dev),
                      GD5F_CMD_PROGRAM_EXECUTE,
                      gd5fPageToRow(physical_page)) &&
       gd5fWaitReady(dev, GD5F_PROGRAM_POLL_LIMIT, &status) &&
       ((status & GD5F_STATUS_P_FAIL) == 0U);
  gd5f_cache_active = false;
  return ok;
}

/**
 * @brief Erase one physical NAND block.
 */
static bool gd5fErasePhysicalBlock(const TagStorageDevice *dev,
                                   uint32_t physical_block)
{
  uint8_t status;

  if (!gd5fWriteEnable(dev))
    return false;
  if (!gd5fRowCommand(tagStorageSpiDevice(dev),
                      GD5F_CMD_BLOCK_ERASE,
                      gd5fBlockToRow(physical_block)))
    return false;
  if (!gd5fWaitReady(dev, GD5F_ERASE_POLL_LIMIT, &status))
    return false;
  return (status & GD5F_STATUS_E_FAIL) == 0U;
}

/**
 * @brief Erase one logical NAND block through the flat map.
 */
static bool gd5fSectorErase(const TagStorageDevice *dev, uint32_t address)
{
  uint32_t logical_block = address / GD5F_BLOCK_SIZE;
  uint32_t physical_block;

  if (!gd5fEnsureLogicalMap())
    return false;
  if (logical_block >= GD5F_LOGICAL_BLOCK_COUNT)
    return true;

  if (!gd5fMapLogicalPage(logical_block * GD5F_PAGES_PER_BLOCK,
                          &physical_block))
    return true;
  physical_block /= GD5F_PAGES_PER_BLOCK;
  if (physical_block >= GD5F_PHYSICAL_BLOCK_COUNT)
    return true;
  return gd5fErasePhysicalBlock(dev, physical_block);
}
/** @} */

/** @name Storage lifecycle callbacks
 * These are the functions exported through TagStorageOps. They bridge the
 * generic storage API to the GD5F1GQ5RE command groups above.
 * @{
 */
/**
 * @brief Begin the storage bus session for this NAND.
 */
static void gd5fWake(const TagStorageDevice *dev)
{
  tagStorageBusBegin(dev);
}

/**
 * @brief End the storage bus session for this NAND.
 */
static void gd5fSleep(const TagStorageDevice *dev)
{
  tagStorageBusEnd(dev);
}

/**
 * @brief Verify chip identity and require a valid provisioned logical map.
 */
static int gd5fCheckID(const TagStorageDevice *dev)
{
  if (gd5fProbe(dev) < 0)
    return -1;

  return gd5fEnsureLogicalMap() ? 0 : -1;
}
/** @} */

const TagStorageOps gd5fStorageOps = {
  .wake = gd5fWake,
  .sleep = gd5fSleep,
  .check_id = gd5fCheckID,
  .write = gd5fWrite,
  .program_load = gd5fProgramLoad,
  .program_load_random = gd5fProgramLoadRandom,
  .program_execute = gd5fProgramExecuteCache,
  .sector_erase = gd5fSectorErase,
  .read = gd5fRead,
};
