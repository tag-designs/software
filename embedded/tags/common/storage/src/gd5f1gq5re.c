/**
 * @file gd5f1gq5re.c
 * @brief GD5F1GQ5RE SPI-NAND command implementation with bad-block mapping.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#include "hal.h"
#include "debug_log.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_gd5f1gq5re.h"
#include "storage_spi.h"

#define GD5F1GQ5RE_CMD_WRITE_ENABLE        0x06U
#define GD5F1GQ5RE_CMD_GET_FEATURE         0x0FU
#define GD5F1GQ5RE_CMD_SET_FEATURE         0x1FU
#define GD5F1GQ5RE_CMD_PAGE_READ           0x13U
#define GD5F1GQ5RE_CMD_READ_CACHE          0x03U
#define GD5F1GQ5RE_CMD_PROGRAM_LOAD        0x02U
#define GD5F1GQ5RE_CMD_PROGRAM_LOAD_RANDOM 0x84U
#define GD5F1GQ5RE_CMD_PROGRAM_EXECUTE     0x10U
#define GD5F1GQ5RE_CMD_BLOCK_ERASE         0xD8U
#define GD5F1GQ5RE_CMD_READ_ID             0x9FU
#define GD5F1GQ5RE_CMD_RESET               0xFFU

#define GD5F1GQ5RE_FEATURE_BLOCK_LOCK      0xA0U
#define GD5F1GQ5RE_FEATURE_STATUS          0xC0U

#define GD5F1GQ5RE_STATUS_OIP              0x01U
#define GD5F1GQ5RE_STATUS_WEL              0x02U
#define GD5F1GQ5RE_STATUS_E_FAIL           0x04U
#define GD5F1GQ5RE_STATUS_P_FAIL           0x08U

#define GD5F1GQ5RE_ID_MANUFACTURER         0xC8U
#define GD5F1GQ5RE_ID_DEVICE_RE            0x41U

#define GD5F1GQ5RE_BAD_BLOCK_MARK_COLUMN   GD5F1GQ5RE_PAGE_SIZE
#define GD5F1GQ5RE_BAD_BLOCK_MARK          0xFFU

#define GD5F1GQ5RE_RESET_DELAY_MS          1U
#define GD5F1GQ5RE_READY_POLL_MS           1U
#define GD5F1GQ5RE_READ_POLL_LIMIT         4U
#define GD5F1GQ5RE_PROGRAM_POLL_LIMIT      8U
#define GD5F1GQ5RE_ERASE_POLL_LIMIT        12U

static uint16_t gd5f1gq5re_bad_blocks[GD5F1GQ5RE_MAX_BAD_BLOCKS];
static uint32_t gd5f1gq5re_bad_block_count;
static bool gd5f1gq5re_bbt_loaded;
static bool gd5f1gq5re_cache_active;
static uint32_t gd5f1gq5re_cache_logical_page;

static bool gd5f1gq5reWriteBytes(const TagSpiDevice *spi,
                                 const uint8_t *buf, uint32_t len)
{
  return tagStorageSpiWrite(spi, buf, len);
}

static bool gd5f1gq5reRowCommand(const TagSpiDevice *spi,
                                 uint8_t command, uint32_t row)
{
  uint8_t buf[4];

  buf[0] = command;
  buf[1] = (uint8_t)(row >> 16);
  buf[2] = (uint8_t)(row >> 8);
  buf[3] = (uint8_t)row;
  return tagSpiBusWrite(spi, buf, sizeof(buf));
}

static bool gd5f1gq5reColumnWrite(const TagSpiDevice *spi,
                                  uint8_t command, uint16_t column,
                                  const uint8_t *buf, uint32_t len)
{
  uint8_t header[3];
  bool ok;

  header[0] = command;
  header[1] = (uint8_t)(column >> 8);
  header[2] = (uint8_t)column;
  tagSpiSelect(spi);
  ok = gd5f1gq5reWriteBytes(spi, header, sizeof(header)) &&
       tagStorageSpiBlockWrite(spi, buf, len);
  tagSpiDeselect(spi);
  return ok;
}

static bool gd5f1gq5reColumnRead(const TagSpiDevice *spi,
                                 uint16_t column, uint8_t *buf, uint32_t len)
{
  uint8_t header[4];
  bool ok;

  header[0] = GD5F1GQ5RE_CMD_READ_CACHE;
  header[1] = (uint8_t)(column >> 8);
  header[2] = (uint8_t)column;
  header[3] = 0U;
  tagSpiSelect(spi);
  ok = gd5f1gq5reWriteBytes(spi, header, sizeof(header)) &&
       tagStorageSpiBlockRead(spi, buf, len);
  tagSpiDeselect(spi);
  return ok;
}

static bool gd5f1gq5reGetFeature(const TagStorageDevice *dev,
                                 uint8_t feature, uint8_t *value)
{
  uint8_t cmd[2];
  bool ok;

  cmd[0] = GD5F1GQ5RE_CMD_GET_FEATURE;
  cmd[1] = feature;
  tagSpiSelect(tagStorageSpiDevice(dev));
  ok = gd5f1gq5reWriteBytes(tagStorageSpiDevice(dev), cmd, sizeof(cmd)) &&
       tagStorageSpiBlockRead(tagStorageSpiDevice(dev), value, 1U);
  tagSpiDeselect(tagStorageSpiDevice(dev));
  return ok;
}

static bool gd5f1gq5reSetFeature(const TagStorageDevice *dev,
                                 uint8_t feature, uint8_t value)
{
  uint8_t cmd[3];

  cmd[0] = GD5F1GQ5RE_CMD_SET_FEATURE;
  cmd[1] = feature;
  cmd[2] = value;
  return tagSpiBusWrite(tagStorageSpiDevice(dev), cmd, sizeof(cmd));
}

static bool gd5f1gq5reWaitReady(const TagStorageDevice *dev,
                                uint32_t poll_limit, uint8_t *status)
{
  uint8_t sr = 0U;

  while (poll_limit-- > 0U) {
    stopMilliseconds(GD5F1GQ5RE_READY_POLL_MS);
    if (!gd5f1gq5reGetFeature(dev, GD5F1GQ5RE_FEATURE_STATUS, &sr))
      return false;
    if ((sr & GD5F1GQ5RE_STATUS_OIP) == 0U) {
      if (status != NULL)
        *status = sr;
      return true;
    }
  }
  return false;
}

static bool gd5f1gq5reWriteEnable(const TagStorageDevice *dev)
{
  uint8_t status;

  if (!tagStorageSpiCommand(tagStorageSpiDevice(dev),
                            GD5F1GQ5RE_CMD_WRITE_ENABLE))
    return false;
  if (!gd5f1gq5reGetFeature(dev, GD5F1GQ5RE_FEATURE_STATUS, &status))
    return false;
  return (status & GD5F1GQ5RE_STATUS_WEL) != 0U;
}

static uint32_t gd5f1gq5rePageToRow(uint32_t physical_page)
{
  return physical_page;
}

static uint32_t gd5f1gq5reBlockToRow(uint32_t physical_block)
{
  return physical_block * GD5F1GQ5RE_PAGES_PER_BLOCK;
}

static bool gd5f1gq5reReadPhysicalPage(const TagStorageDevice *dev,
                                       uint32_t physical_page,
                                       uint16_t column,
                                       uint8_t *buf, uint32_t len)
{
  uint8_t status;

  if (!gd5f1gq5reRowCommand(tagStorageSpiDevice(dev),
                            GD5F1GQ5RE_CMD_PAGE_READ,
                            gd5f1gq5rePageToRow(physical_page)))
    return false;
  if (!gd5f1gq5reWaitReady(dev, GD5F1GQ5RE_READ_POLL_LIMIT, &status))
    return false;
  return gd5f1gq5reColumnRead(tagStorageSpiDevice(dev), column, buf, len);
}

static bool gd5f1gq5reProgramPhysicalPage(const TagStorageDevice *dev,
                                          uint32_t physical_page,
                                          const uint8_t *buf)
{
  uint8_t status;

  if (!gd5f1gq5reWriteEnable(dev))
    return false;
  if (!gd5f1gq5reColumnWrite(tagStorageSpiDevice(dev),
                             GD5F1GQ5RE_CMD_PROGRAM_LOAD, 0U,
                             buf, GD5F1GQ5RE_PAGE_SIZE))
    return false;
  if (!gd5f1gq5reWriteEnable(dev))
    return false;
  if (!gd5f1gq5reRowCommand(tagStorageSpiDevice(dev),
                            GD5F1GQ5RE_CMD_PROGRAM_EXECUTE,
                            gd5f1gq5rePageToRow(physical_page)))
    return false;
  if (!gd5f1gq5reWaitReady(dev, GD5F1GQ5RE_PROGRAM_POLL_LIMIT, &status))
    return false;
  return (status & GD5F1GQ5RE_STATUS_P_FAIL) == 0U;
}

static bool gd5f1gq5reErasePhysicalBlock(const TagStorageDevice *dev,
                                         uint32_t physical_block)
{
  uint8_t status;

  if (!gd5f1gq5reWriteEnable(dev))
    return false;
  if (!gd5f1gq5reRowCommand(tagStorageSpiDevice(dev),
                            GD5F1GQ5RE_CMD_BLOCK_ERASE,
                            gd5f1gq5reBlockToRow(physical_block)))
    return false;
  if (!gd5f1gq5reWaitReady(dev, GD5F1GQ5RE_ERASE_POLL_LIMIT, &status))
    return false;
  return (status & GD5F1GQ5RE_STATUS_E_FAIL) == 0U;
}

static uint32_t gd5f1gq5reBbtChecksumUpdate(uint32_t hash, uint8_t value)
{
  hash ^= value;
  hash *= 16777619UL;
  return hash;
}

static bool gd5f1gq5reBadListIsValid(const uint16_t *bad_blocks,
                                     uint32_t bad_block_count)
{
  if (bad_block_count > GD5F1GQ5RE_MAX_BAD_BLOCKS)
    return false;
  if ((GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT - bad_block_count) <
      GD5F1GQ5RE_LOGICAL_BLOCK_COUNT)
    return false;

  for (uint32_t i = 0; i < bad_block_count; i++) {
    if (bad_blocks[i] >= GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT)
      return false;
    if (i > 0U && bad_blocks[i - 1U] >= bad_blocks[i])
      return false;
  }
  return true;
}

static uint32_t gd5f1gq5reBadListChecksum(uint32_t bad_block_count,
                                          const uint16_t *bad_blocks)
{
  uint32_t hash = 2166136261UL;

  hash = gd5f1gq5reBbtChecksumUpdate(hash, (uint8_t)bad_block_count);
  hash = gd5f1gq5reBbtChecksumUpdate(hash, (uint8_t)(bad_block_count >> 8));
  hash = gd5f1gq5reBbtChecksumUpdate(hash, (uint8_t)(bad_block_count >> 16));
  hash = gd5f1gq5reBbtChecksumUpdate(hash, (uint8_t)(bad_block_count >> 24));

  for (uint32_t i = 0; i < bad_block_count; i++) {
    hash = gd5f1gq5reBbtChecksumUpdate(hash, (uint8_t)bad_blocks[i]);
    hash = gd5f1gq5reBbtChecksumUpdate(hash,
                                       (uint8_t)(bad_blocks[i] >> 8));
  }
  return hash;
}

static bool gd5f1gq5reLoadCpuBbt(void)
{
  const TagGd5f1gq5reBadBlockTable *table = &gd5f1gq5reBadBlockTable;

  if (table == NULL)
    return false;
  if (table->magic != GD5F1GQ5RE_BBT_MAGIC ||
      table->version != GD5F1GQ5RE_BBT_VERSION ||
      table->physical_block_count != GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT ||
      table->logical_block_count != GD5F1GQ5RE_LOGICAL_BLOCK_COUNT)
    return false;
  if (!gd5f1gq5reBadListIsValid(table->bad_blocks,
                                table->bad_block_count))
    return false;
  if (gd5f1gq5reBadListChecksum(table->bad_block_count,
                                table->bad_blocks) != table->checksum)
    return false;

  gd5f1gq5re_bad_block_count = table->bad_block_count;
  for (uint32_t i = 0; i < gd5f1gq5re_bad_block_count; i++)
    gd5f1gq5re_bad_blocks[i] = table->bad_blocks[i];
  return true;
}

static bool gd5f1gq5reBlockIsGood(const TagStorageDevice *dev,
                                  uint32_t physical_block)
{
  uint8_t mark = 0U;
  uint32_t physical_page = physical_block * GD5F1GQ5RE_PAGES_PER_BLOCK;

  if (!gd5f1gq5reReadPhysicalPage(dev, physical_page,
                                  GD5F1GQ5RE_BAD_BLOCK_MARK_COLUMN,
                                  &mark, 1U))
    return false;
  return mark == GD5F1GQ5RE_BAD_BLOCK_MARK;
}

static bool gd5f1gq5reBuildBbt(const TagStorageDevice *dev)
{
  uint32_t bad_count = 0U;

  for (uint32_t block = 0U; block < GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT; block++) {
    if (!gd5f1gq5reBlockIsGood(dev, block)) {
      if (bad_count >= GD5F1GQ5RE_MAX_BAD_BLOCKS)
        return false;
      gd5f1gq5re_bad_blocks[bad_count++] = (uint16_t)block;
    }
  }

  if ((GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT - bad_count) <
      GD5F1GQ5RE_LOGICAL_BLOCK_COUNT)
    return false;

  gd5f1gq5re_bad_block_count = bad_count;
  return true;
}

static bool gd5f1gq5reEnsureBbt(const TagStorageDevice *dev)
{
  if (gd5f1gq5re_bbt_loaded)
    return true;

  if (!gd5f1gq5reLoadCpuBbt()) {
    if (!gd5f1gq5reBuildBbt(dev))
      return false;
  }
  gd5f1gq5re_bbt_loaded = true;
  return true;
}

static bool gd5f1gq5reMapLogicalPage(uint32_t logical_page,
                                     uint32_t *physical_page)
{
  uint32_t logical_block = logical_page / GD5F1GQ5RE_PAGES_PER_BLOCK;
  uint32_t page_in_block = logical_page % GD5F1GQ5RE_PAGES_PER_BLOCK;
  uint32_t good_blocks_seen = 0U;
  uint32_t bad_index = 0U;

  if (logical_block >= GD5F1GQ5RE_LOGICAL_BLOCK_COUNT)
    return false;

  for (uint32_t block = 0U; block < GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT; block++) {
    if (bad_index < gd5f1gq5re_bad_block_count &&
        gd5f1gq5re_bad_blocks[bad_index] == block) {
      bad_index++;
      continue;
    }

    if (good_blocks_seen == logical_block) {
      *physical_page = (block * GD5F1GQ5RE_PAGES_PER_BLOCK) + page_in_block;
      return true;
    }
    good_blocks_seen++;
  }

  return false;
}

static void gd5f1gq5reWake(const TagStorageDevice *dev)
{
  tagStorageBusBegin(dev);
}

static void gd5f1gq5reSleep(const TagStorageDevice *dev)
{
  tagStorageBusEnd(dev);
}

static int gd5f1gq5reCheckID(const TagStorageDevice *dev)
{
  uint8_t cmd[2] = { GD5F1GQ5RE_CMD_READ_ID, 0U };
  uint8_t id[2] = { 0U, 0U };
  bool ok;

  tagStorageSpiCommand(tagStorageSpiDevice(dev), GD5F1GQ5RE_CMD_RESET);
  stopMilliseconds(GD5F1GQ5RE_RESET_DELAY_MS);
  gd5f1gq5reSetFeature(dev, GD5F1GQ5RE_FEATURE_BLOCK_LOCK, 0U);

  tagSpiSelect(tagStorageSpiDevice(dev));
  ok = gd5f1gq5reWriteBytes(tagStorageSpiDevice(dev), cmd, sizeof(cmd)) &&
       tagStorageSpiBlockRead(tagStorageSpiDevice(dev), id, sizeof(id));
  tagSpiDeselect(tagStorageSpiDevice(dev));
  if (!ok)
    return -1;

  if (MONCONNECTED)
    debug_log_printf("NAND: MID 0x%x DID 0x%x", id[0], id[1]);

  if (id[0] != GD5F1GQ5RE_ID_MANUFACTURER ||
      id[1] != GD5F1GQ5RE_ID_DEVICE_RE)
    return -1;

  return gd5f1gq5reEnsureBbt(dev) ? 0 : -1;
}

static bool gd5f1gq5reWrite(const TagStorageDevice *dev, uint32_t address,
                            uint8_t *buf, int *cnt)
{
  int requested = *cnt;
  int written = 0;

  *cnt = 0;
  if (!gd5f1gq5reEnsureBbt(dev))
    return false;
  if ((address % GD5F1GQ5RE_PAGE_SIZE) != 0U ||
      (requested % (int)GD5F1GQ5RE_PAGE_SIZE) != 0)
    return false;

  while (requested > 0) {
    uint32_t physical_page;
    uint32_t logical_page = address / GD5F1GQ5RE_PAGE_SIZE;

    if (!gd5f1gq5reMapLogicalPage(logical_page, &physical_page))
      return false;
    if (!gd5f1gq5reProgramPhysicalPage(dev, physical_page, buf))
      return false;

    address += GD5F1GQ5RE_PAGE_SIZE;
    buf += GD5F1GQ5RE_PAGE_SIZE;
    requested -= (int)GD5F1GQ5RE_PAGE_SIZE;
    written += (int)GD5F1GQ5RE_PAGE_SIZE;
    *cnt = written;
  }
  return true;
}

static bool gd5f1gq5reProgramCacheLoad(const TagStorageDevice *dev,
                                       uint32_t address,
                                       const uint8_t *buf,
                                       int cnt,
                                       bool random)
{
  uint32_t logical_page = address / GD5F1GQ5RE_PAGE_SIZE;
  uint16_t column = (uint16_t)(address % GD5F1GQ5RE_PAGE_SIZE);
  uint8_t command = random ? GD5F1GQ5RE_CMD_PROGRAM_LOAD_RANDOM :
                             GD5F1GQ5RE_CMD_PROGRAM_LOAD;

  if (buf == NULL || cnt <= 0)
    return false;
  if (!gd5f1gq5reEnsureBbt(dev))
    return false;
  if ((uint32_t)cnt > (GD5F1GQ5RE_PAGE_SIZE - column))
    return false;
  if (logical_page >=
      (GD5F1GQ5RE_LOGICAL_BLOCK_COUNT * GD5F1GQ5RE_PAGES_PER_BLOCK))
    return false;
  if (random && (!gd5f1gq5re_cache_active ||
                 gd5f1gq5re_cache_logical_page != logical_page))
    return false;
  if (!random) {
    gd5f1gq5re_cache_active = false;
    gd5f1gq5re_cache_logical_page = logical_page;
  }

  if (!gd5f1gq5reWriteEnable(dev))
    return false;
  if (!gd5f1gq5reColumnWrite(tagStorageSpiDevice(dev), command, column, buf,
                             (uint32_t)cnt))
    return false;

  gd5f1gq5re_cache_active = true;
  return true;
}

static bool gd5f1gq5reProgramLoad(const TagStorageDevice *dev,
                                  uint32_t address,
                                  const uint8_t *buf,
                                  int cnt)
{
  return gd5f1gq5reProgramCacheLoad(dev, address, buf, cnt, false);
}

static bool gd5f1gq5reProgramLoadRandom(const TagStorageDevice *dev,
                                        uint32_t address,
                                        const uint8_t *buf,
                                        int cnt)
{
  return gd5f1gq5reProgramCacheLoad(dev, address, buf, cnt, true);
}

static bool gd5f1gq5reProgramExecuteCache(const TagStorageDevice *dev,
                                          uint32_t address)
{
  uint8_t status;
  uint32_t physical_page;
  uint32_t logical_page = address / GD5F1GQ5RE_PAGE_SIZE;
  bool ok;

  if (!gd5f1gq5reEnsureBbt(dev))
    return false;
  if (!gd5f1gq5re_cache_active ||
      gd5f1gq5re_cache_logical_page != logical_page)
    return false;
  if (!gd5f1gq5reMapLogicalPage(logical_page, &physical_page))
    return false;

  ok = gd5f1gq5reWriteEnable(dev) &&
       gd5f1gq5reRowCommand(tagStorageSpiDevice(dev),
                            GD5F1GQ5RE_CMD_PROGRAM_EXECUTE,
                            gd5f1gq5rePageToRow(physical_page)) &&
       gd5f1gq5reWaitReady(dev, GD5F1GQ5RE_PROGRAM_POLL_LIMIT, &status) &&
       ((status & GD5F1GQ5RE_STATUS_P_FAIL) == 0U);
  gd5f1gq5re_cache_active = false;
  return ok;
}

static bool gd5f1gq5reSectorErase(const TagStorageDevice *dev,
                                  uint32_t address)
{
  uint32_t logical_block = address / GD5F1GQ5RE_BLOCK_SIZE;
  uint32_t physical_block;

  if (!gd5f1gq5reEnsureBbt(dev))
    return false;
  if (logical_block >= GD5F1GQ5RE_LOGICAL_BLOCK_COUNT)
    return true;

  if (!gd5f1gq5reMapLogicalPage(logical_block * GD5F1GQ5RE_PAGES_PER_BLOCK,
                                &physical_block))
    return true;
  physical_block /= GD5F1GQ5RE_PAGES_PER_BLOCK;
  if (physical_block >= GD5F1GQ5RE_PHYSICAL_BLOCK_COUNT)
    return true;
  return gd5f1gq5reErasePhysicalBlock(dev, physical_block);
}

static void gd5f1gq5reRead(const TagStorageDevice *dev, uint32_t address,
                           uint8_t *buf, int num)
{
  if (!gd5f1gq5reEnsureBbt(dev))
    return;

  while (num > 0) {
    uint32_t physical_page;
    uint32_t logical_page = address / GD5F1GQ5RE_PAGE_SIZE;
    uint16_t column = (uint16_t)(address % GD5F1GQ5RE_PAGE_SIZE);
    uint32_t bytes = GD5F1GQ5RE_PAGE_SIZE - column;

    if (bytes > (uint32_t)num)
      bytes = (uint32_t)num;
    if (!gd5f1gq5reMapLogicalPage(logical_page, &physical_page))
      return;
    if (!gd5f1gq5reReadPhysicalPage(dev, physical_page, column, buf, bytes))
      return;

    address += bytes;
    buf += bytes;
    num -= (int)bytes;
  }
}

const TagStorageOps gd5f1gq5reStorageOps = {
  .wake = gd5f1gq5reWake,
  .sleep = gd5f1gq5reSleep,
  .check_id = gd5f1gq5reCheckID,
  .write = gd5f1gq5reWrite,
  .program_load = gd5f1gq5reProgramLoad,
  .program_load_random = gd5f1gq5reProgramLoadRandom,
  .program_execute = gd5f1gq5reProgramExecuteCache,
  .sector_erase = gd5f1gq5reSectorErase,
  .read = gd5f1gq5reRead,
};
