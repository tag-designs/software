/**
 * @file    gd5f_loader.c
 * @brief   GD5F SPI-NAND part driver for external loaders: read-only.
 *
 * @details Implements loader_flash.h for GigaDevice GD5F SPI NAND, using the
 *          firmware's own command set (tags/common/storage/inc/gd5f_commands.h)
 *          and the per-part geometry defines passed by the loader's project.mk.
 *
 *          Its one write is B0h ECC_EN, to read pages raw (gd5fSetEcc(); plan
 *          decision 2), and Serve() restores B0h as found before it returns.
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
#define GD5F_LOADER_BLOCK_BYTES (GD5F_PAGE_SIZE * GD5F_PAGES_PER_BLOCK) ///< Data bytes per block.
#define GD5F_LOADER_DATA_BYTES  (GD5F_LOADER_BLOCK_BYTES * GD5F_PHYSICAL_BLOCK_COUNT) ///< Data bytes in the part.
#define GD5F_LOADER_PAGE_COUNT  (GD5F_PAGES_PER_BLOCK * GD5F_PHYSICAL_BLOCK_COUNT) ///< Pages in the part.
/** @} */

/** @brief Keeps an entry point linked and in the section the map retains. */
#define GD5F_LOADER_ENTRY __attribute__((used, noinline, section(".loader_entry")))

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

/** @brief A0h, B0h, C0h, F0h as the probe found them, before any change. */
static uint8_t gd5f_found[4];
/** @brief B0h as last written or found; differs from gd5f_found[1] after a mode change. */
static uint8_t gd5f_b0_now;

/** @brief Bytes per whole page: data plus spare. */
#define GD5F_LOADER_PAGE_BYTES (GD5F_PAGE_SIZE + GD5F_SPARE_SIZE)

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
 * @brief   Write one feature register and read it back.
 *
 * @warning B0h bit 7 (OTP_PRT) is non-volatile. Callers write B0h only through
 *          gd5fSetEcc(), which never sets it.
 */
static bool gd5fSetFeature(uint8_t addr, uint8_t value)
{
  const uint8_t cmd[3] = {GD5F_CMD_SET_FEATURE, addr, value};
  uint8_t back = 0U;
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiSend(&loaderFlashBus, cmd, sizeof(cmd));
  loaderSpiDeselect(&loaderFlashBus);
  return ok && gd5fGetFeature(addr, &back) && back == value;
}

/**
 * @brief   Select on-die ECC on or off for page reads, changing nothing else.
 *
 * @details The only feature write this loader makes (u375-nand-loader-plan.md,
 *          decision 2). The new B0h value is the value found with only ECC_EN
 *          changed and the reserved bits low. It is refused if OTP_EN or
 *          OTP_PRT was found set: OTP_PRT is non-volatile, and with OTP_EN
 *          set page reads address the OTP area, so neither mode is meaningful.
 *
 * @return  false when refused, or when the write did not read back.
 */
static bool gd5fSetEcc(bool on)
{
  const uint8_t found = gd5f_found[1];
  uint8_t want;

  if ((found & (GD5F_CONFIG_OTP_PRT | GD5F_CONFIG_OTP_EN)) != 0U)
    return (gd5f_b0_now & GD5F_CONFIG_ECC_EN) == (on ? GD5F_CONFIG_ECC_EN : 0U);
  want = (uint8_t)(found & ~(GD5F_CONFIG_ECC_EN | GD5F_CONFIG_RESERVED));
  if (on)
    want |= GD5F_CONFIG_ECC_EN;
  if (want == gd5f_b0_now)
    return true;
  if (!gd5fSetFeature(GD5F_FEATURE_CONFIG, want))
    return false;
  gd5f_b0_now = want;
  return true;
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
  if (!ok || id[0] != GD5F_ID_MANUFACTURER || id[1] != GD5F_ID_DEVICE)
    return false;

  /* The configuration as found, before anything changes it. */
  if (!gd5fGetFeature(GD5F_FEATURE_BLOCK_LOCK, &gd5f_found[0]) ||
      !gd5fGetFeature(GD5F_FEATURE_CONFIG, &gd5f_found[1]) ||
      !gd5fGetFeature(GD5F_FEATURE_STATUS, &gd5f_found[2]) ||
      !gd5fGetFeature(GD5F_FEATURE_STATUS2, &gd5f_found[3]))
    return false;
  gd5f_b0_now = gd5f_found[1];
  return true;
}

/** @brief Bytes per whole page, data plus spare (loader_flash.h). */
uint32_t loaderFlashPageBytes(void)
{
  return GD5F_LOADER_PAGE_BYTES;
}

/** @brief Number of pages in the part (loader_flash.h). */
uint32_t loaderFlashPageCount(void)
{
  return GD5F_LOADER_PAGE_COUNT;
}

/**
 * @brief   The configuration loaderFlashProbe() found (loader_flash.h).
 * @return  A0h | B0h << 8 | C0h << 16 | F0h << 24.
 */
uint32_t loaderFlashFound(void)
{
  return (uint32_t)gd5f_found[0] | ((uint32_t)gd5f_found[1] << 8) |
         ((uint32_t)gd5f_found[2] << 16) | ((uint32_t)gd5f_found[3] << 24);
}

/**
 * @brief   Read one whole page, data and spare, raw or through on-die ECC
 *          (loader_flash.h).
 *
 * @details Sets ECC_EN as the mode needs (gd5fSetEcc()), then page read,
 *          cache read and a status read proving the part answered throughout,
 *          retrying a page that overlapped an outage.
 *
 * @param[in]  page     Physical page index.
 * @param[in]  raw      true for ECC off, false for on-die ECC.
 * @param[out] buf      GD5F_PAGE_SIZE + GD5F_SPARE_SIZE bytes.
 * @param[out] status   C0h after the page read: bits 5:4 are the ECC verdict.
 * @param[out] status2  F0h after the page read.
 * @return  false on a range error, a refused mode change, or a failed read.
 */
bool loaderFlashReadPage(uint32_t page, bool raw, uint8_t *buf,
                         uint8_t *status, uint8_t *status2)
{
  uint32_t attempt;
  uint8_t after;

  if (page >= GD5F_LOADER_PAGE_COUNT || !gd5fSetEcc(!raw))
    return false;
  for (attempt = 0; attempt < GD5F_LOADER_PAGE_RETRIES; attempt++) {
    if (attempt > 0U)
      loaderDelayMs(GD5F_LOADER_RETRY_WAIT_MS);
    if (!gd5fPageRead(page, status) ||
        !gd5fReadCache(0U, buf, GD5F_LOADER_PAGE_BYTES))
      continue;
    /* Answering after the transfer: the bytes were all driven by the part. */
    if (!gd5fGetFeature(GD5F_FEATURE_STATUS, &after))
      return false;
    if (after == GD5F_LOADER_NO_REPLY)
      continue;
    return gd5fGetFeature(GD5F_FEATURE_STATUS2, status2);
  }
  return false;
}

/**
 * @brief   Put B0h back as found, if a raw read changed it (loader_flash.h).
 * @return  false if the restore did not read back.
 */
bool loaderFlashRestore(void)
{
  uint8_t back = 0U;

  if (gd5f_b0_now == gd5f_found[1])
    return true;
  if (!gd5fSetFeature(GD5F_FEATURE_CONFIG, gd5f_found[1]))
    return gd5fGetFeature(GD5F_FEATURE_CONFIG, &back) && back == gd5f_found[1];
  gd5f_b0_now = gd5f_found[1];
  return true;
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
GD5F_LOADER_ENTRY int NandInfo(uint8_t *buf)
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
