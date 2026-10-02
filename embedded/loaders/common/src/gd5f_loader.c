/**
 * @file    gd5f_loader.c
 * @brief   GD5F SPI-NAND part driver for external loaders: read-only.
 *
 * @details Implements loader_flash.h for GigaDevice GD5F SPI NAND, using the
 *          firmware's own command set (tags/common/storage/inc/gd5f_commands.h)
 *          and the per-part geometry defines passed by the loader's project.mk.
 *
 *          What it never does, by design (u375-nand-loader-plan.md, decision 1):
 *          - send Reset (FFh), which would clear the status of the part's last
 *            operation;
 *          - write the block-lock register (A0h), whose value as found is
 *            evidence;
 *          - erase or program. The image contains no such code.
 *
 *          The firmware's gd5fProbe() does the first two on every boot, so it
 *          cannot be reused here.
 *
 *          The ST entry points see a linear view of the data area only:
 *          offset = page * GD5F_PAGE_SIZE + column, read through on-die ECC as
 *          the part is found configured. Spare bytes and raw reads are served
 *          through Serve().
 */

#include "loader_flash.h"

#include "gd5f_commands.h"

/** @name Geometry, from the project's -D defines
 * @{
 */
#define GD5F_LOADER_BLOCK_BYTES (GD5F_PAGE_SIZE * GD5F_PAGES_PER_BLOCK)
#define GD5F_LOADER_DATA_BYTES  (GD5F_LOADER_BLOCK_BYTES * GD5F_PHYSICAL_BLOCK_COUNT)
#define GD5F_LOADER_PAGE_COUNT  (GD5F_PAGES_PER_BLOCK * GD5F_PHYSICAL_BLOCK_COUNT)
/** @} */

/** @brief Interval between status polls while a page read completes. */
#define GD5F_LOADER_POLL_US 10U
/** @brief Status polls before a page read is declared hung: 10x tRD_ECC max. */
#define GD5F_LOADER_POLL_LIMIT ((10U * GD5F_TRD_ECC_MAX_US) / GD5F_LOADER_POLL_US)

/**
 * @brief A status reply of all ones: nothing was driving MISO.
 *
 * @details Seen on the bench before the loader powered the NAND through
 *          FLASH_PWR: the part, running on residual charge, answered for a few
 *          tens of milliseconds and then dropped out, first floating MISO high
 *          and then low. A cache read in progress came back as FFh from the
 *          point of the outage, silently. The board now powers the part
 *          (board_loader.c), and this check stays as cheap insurance: a page is
 *          returned only when a status read after it shows the part answering.
 */
#define GD5F_LOADER_NO_REPLY 0xFFU
/** @brief Attempts at a page whose read overlapped an outage. */
#define GD5F_LOADER_PAGE_RETRIES 5U
/** @brief Wait before retrying such a page. */
#define GD5F_LOADER_RETRY_WAIT_MS 100U

/** @brief ID read by the last probe: manufacturer << 16 | device << 8. */
static uint32_t gd5f_id;

/** @brief Run one command byte with chip select framed around it. */
static bool gd5fCommand(uint8_t cmd)
{
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiExchange(&loaderFlashBus, cmd, NULL);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/** @brief Read one feature register. */
static bool gd5fGetFeature(uint8_t addr, uint8_t *value)
{
  const uint8_t hdr[2] = {GD5F_CMD_GET_FEATURE, addr};
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiSend(&loaderFlashBus, hdr, sizeof(hdr)) &&
       loaderSpiReceive(&loaderFlashBus, value, 1U);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/**
 * @brief   Load @p page into the part's cache and wait for it.
 *
 * @param[in]  page    Physical page index (block * pages-per-block + page).
 * @param[out] status  Status register C0 after the read: ECCS holds the
 *                     on-die ECC verdict for this page.
 * @return  false on an SPI timeout or when OIP never cleared.
 */
static bool gd5fPageRead(uint32_t page, uint8_t *status)
{
  const uint8_t hdr[4] = {GD5F_CMD_PAGE_READ, (uint8_t)(page >> 16),
                          (uint8_t)(page >> 8), (uint8_t)page};
  bool ok;
  uint32_t n;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiSend(&loaderFlashBus, hdr, sizeof(hdr));
  loaderSpiDeselect(&loaderFlashBus);
  if (!ok)
    return false;

  for (n = 0; n < GD5F_LOADER_POLL_LIMIT; n++) {
    loaderDelayUs(GD5F_LOADER_POLL_US);
    if (!gd5fGetFeature(GD5F_FEATURE_STATUS, status))
      return false;
    if (*status == GD5F_LOADER_NO_REPLY)
      return false;   /* not busy: absent (see GD5F_LOADER_NO_REPLY) */
    if ((*status & GD5F_STATUS_OIP) == 0U)
      return true;
  }
  return false;
}

/** @brief Read @p n bytes of the cache from @p column. */
static bool gd5fReadCache(uint32_t column, uint8_t *buf, uint32_t n)
{
  const uint8_t hdr[4] = {GD5F_CMD_READ_CACHE, (uint8_t)(column >> 8),
                          (uint8_t)column, 0x00U};
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiSend(&loaderFlashBus, hdr, sizeof(hdr)) &&
       loaderSpiReceive(&loaderFlashBus, buf, n);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/* Contract documented in loader_flash.h. */
bool loaderFlashProbe(void)
{
  uint8_t id[2] = {0U, 0U};
  bool ok;

  /*
   * The firmware puts the part in deep power-down at Standby entry. Release
   * (ABh) is harmless when it is not, and is not a reset.
   */
  if (!gd5fCommand(GD5F_CMD_RELEASE_DPD))
    return false;
  loaderDelayUs(GD5F_DPD_RELEASE_DELAY_US);

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiExchange(&loaderFlashBus, GD5F_CMD_READ_ID, NULL) &&
       loaderSpiExchange(&loaderFlashBus, 0x00U, NULL) &&   /* dummy */
       loaderSpiReceive(&loaderFlashBus, id, sizeof(id));
  loaderSpiDeselect(&loaderFlashBus);

  gd5f_id = ok ? (((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8)) : 0U;
  return ok && id[0] == GD5F_ID_MANUFACTURER && id[1] == GD5F_ID_DEVICE;
}

/* Contract documented in loader_flash.h. For NAND, sr1 is status C0. */
bool loaderFlashIdentity(uint32_t *jedec, uint8_t *sr1)
{
  *jedec = gd5f_id;
  return gd5fGetFeature(GD5F_FEATURE_STATUS, sr1);
}

/* Contract documented in loader_flash.h: the data area only. */
uint32_t loaderFlashSize(void)
{
  return GD5F_LOADER_DATA_BYTES;
}

/* Contract documented in loader_flash.h: one 64-page block. */
uint32_t loaderFlashSectorSize(void)
{
  return GD5F_LOADER_BLOCK_BYTES;
}

/**
 * @brief   Read @p n bytes of @p page from @p column, proving the part was
 *          answering throughout.
 *
 * @details A page read and cache read, then a status read. If the status
 *          reply is all ones, the part stopped driving MISO at some point
 *          during the transfer, so the bytes may be partly FFh that the part
 *          never sent. The page is then re-read after a wait, a bounded number
 *          of times. A transfer is only returned once a status read after it
 *          shows the part answering.
 *
 * @return  false when the page could not be read cleanly.
 */
static bool gd5fReadVerified(uint32_t page, uint32_t column, uint8_t *buf,
                             uint32_t n)
{
  uint32_t attempt;
  uint8_t status;

  for (attempt = 0; attempt < GD5F_LOADER_PAGE_RETRIES; attempt++) {
    if (attempt > 0U)
      loaderDelayMs(GD5F_LOADER_RETRY_WAIT_MS);
    if (!gd5fPageRead(page, &status) || !gd5fReadCache(column, buf, n))
      continue;
    if (!gd5fGetFeature(GD5F_FEATURE_STATUS, &status))
      return false;
    if (status != GD5F_LOADER_NO_REPLY)
      return true;
  }
  return false;
}

/* Contract documented in loader_flash.h: linear data view, ECC as found. */
bool loaderFlashRead(uint32_t offset, uint8_t *buf, uint32_t n)
{
  while (n > 0U) {
    const uint32_t page = offset / GD5F_PAGE_SIZE;
    const uint32_t column = offset % GD5F_PAGE_SIZE;
    const uint32_t chunk = (n < GD5F_PAGE_SIZE - column) ? n : GD5F_PAGE_SIZE - column;

    if (page >= GD5F_LOADER_PAGE_COUNT ||
        !gd5fReadVerified(page, column, buf, chunk))
      return false;
    buf += chunk;
    offset += chunk;
    n -= chunk;
  }
  return true;
}

/**
 * @brief   Report the part's identity and feature registers as found.
 *
 * @details A diagnostic entry point for tag-sramcall, made for step 2 of the
 *          U375 loader plan. Brings up clock, delay, board and SPI as
 *          loaderStart() does, probes, then reads A0, B0, C0 and F0. Writes:
 *          buf[0..1] the ID bytes, buf[2] A0, buf[3] B0, buf[4] C0, buf[5] F0.
 *          Nothing is written to the part.
 *
 * @param[out] buf  At least 8 bytes in SRAM.
 * @return  1 when every read succeeded and the ID matched, 0 otherwise.
 */
__attribute__((used, noinline, section(".loader_entry")))
int NandInfo(uint8_t *buf)
{
  bool ok;

  __disable_irq();
  if (!loaderClockInit())
    return 0;
  loaderDelayInit();
  if (!loaderBoardInit())
    return 0;
  loaderSpiInit(&loaderFlashBus);

  ok = loaderFlashProbe();
  buf[0] = (uint8_t)(gd5f_id >> 16);
  buf[1] = (uint8_t)(gd5f_id >> 8);
  ok = gd5fGetFeature(GD5F_FEATURE_BLOCK_LOCK, &buf[2]) && ok;
  ok = gd5fGetFeature(GD5F_FEATURE_CONFIG, &buf[3]) && ok;
  ok = gd5fGetFeature(GD5F_FEATURE_STATUS, &buf[4]) && ok;
  ok = gd5fGetFeature(GD5F_FEATURE_STATUS2, &buf[5]) && ok;
  return ok ? 1 : 0;
}
