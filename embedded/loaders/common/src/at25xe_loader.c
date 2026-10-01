/**
 * @file    at25xe_loader.c
 * @brief   AT25XE part driver for external loaders: read, and optionally
 *          erase and program with read-back verification.
 *
 * @details A loader-specific driver rather than the firmware's at25xe.c. The
 *          firmware driver is built around the tag's bus lifecycle, power
 *          policy and kernel sleeps; a loader needs none of that. The two
 *          share at25xe_commands.h, so opcodes and measured timing budgets
 *          exist once.
 *
 *          Reading is non-mutating: probing sends Resume from Power-Down and
 *          reads the identity, and never writes a status register. Found
 *          protection bits are evidence, and the read-only image contains no
 *          code that could change them.
 *
 *          Erase (LOADER_ALLOW_WRITE=1 only) works in 4 kB sectors -- the
 *          command and budget the firmware has measured -- and defines
 *          success by reading the sector back as all 0xFF. That catches the
 *          part's silent refusal to erase a protected block, which reports no
 *          error and never raises BSY, and catches a budget that proves too
 *          short. Sectors that already read blank are not erased.
 *
 *          Program (LOADER_ALLOW_WRITE=1 only) uses Page Program within the
 *          firmware's page-program budget and is likewise judged by reading
 *          back what was written.
 *
 * @see     Renesas DS-AT25XE321D-160 Rev. P.
 */

#include "loader_flash.h"

#include "at25xe_commands.h"

/** @brief Erased-byte value of the array. */
#define AT25XE_ERASED_BYTE 0xFFU

/** JEDEC ID read by the last loaderFlashProbe(), manufacturer in bits 23:16. */
static uint32_t at25_jedec;

/**
 * @brief   Send a single-byte command under its own chip select.
 *
 * @return  false on an SPI timeout.
 */
static bool at25Command(uint8_t cmd)
{
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiExchange(&loaderFlashBus, cmd, NULL);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/**
 * @brief   Send a command and 24-bit address, leaving chip select asserted.
 *
 * @details The caller continues the transaction and must deselect.
 *
 * @return  false on an SPI timeout.
 */
static bool at25BeginAddressed(uint8_t cmd, uint32_t offset)
{
  const uint8_t header[4] = {
      cmd,
      (uint8_t)(offset >> 16),
      (uint8_t)(offset >> 8),
      (uint8_t)offset,
  };

  loaderSpiSelect(&loaderFlashBus);
  return loaderSpiSend(&loaderFlashBus, header, sizeof(header));
}

/* Contract documented in loader_flash.h. */
bool loaderFlashProbe(void)
{
  uint8_t id[3];
  bool ok;

  /*
   * ABh leaves both Deep and Ultra-Deep Power-Down; from UDPD it performs an
   * internal reset. The tag leaves the part in UDPD between checkpoints, and
   * on this part deasserting CS alone does not exit UDPD.
   */
  if (!at25Command(AT25XE_CMD_POWER_UP))
    return false;
  loaderDelayMs(AT25XE_WAKE_DELAY_MS);

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiExchange(&loaderFlashBus, AT25XE_CMD_READ_ID, NULL) &&
       loaderSpiReceive(&loaderFlashBus, id, sizeof(id));
  loaderSpiDeselect(&loaderFlashBus);

  at25_jedec = ok ? ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2]
                  : 0U;
  return ok && id[0] == AT25XE_JEDEC_MANUFACTURER &&
         id[1] == AT25XE_JEDEC_DEVICE1;
}

/* Contract documented in loader_flash.h. */
uint32_t loaderFlashSize(void)
{
  return AT25XE_SIZE;
}

/* Contract documented in loader_flash.h. */
uint32_t loaderFlashSectorSize(void)
{
  return AT25XE_SECTOR_SIZE;
}

/* Contract documented in loader_flash.h. */
bool loaderFlashRead(uint32_t offset, uint8_t *buf, uint32_t n)
{
  bool ok;

  /* One Read Array command streams the whole range. */
  ok = at25BeginAddressed(AT25XE_CMD_READ, offset) &&
       loaderSpiReceive(&loaderFlashBus, buf, n);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/**
 * @brief   Read Status Register 1.
 *
 * @param[out] status   Register value.
 * @return  false on an SPI timeout.
 */
static bool at25ReadStatus(uint8_t *status)
{
  bool ok;

  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiExchange(&loaderFlashBus, AT25XE_CMD_READ_STATUS_REG, NULL) &&
       loaderSpiExchange(&loaderFlashBus, 0xFFU, status);
  loaderSpiDeselect(&loaderFlashBus);
  return ok;
}

/* Contract documented in loader_flash.h. */
bool loaderFlashIdentity(uint32_t *jedec, uint8_t *sr1)
{
  *jedec = at25_jedec;
  return at25ReadStatus(sr1);
}

#if LOADER_ALLOW_WRITE
/**
 * @brief   Poll until BSY clears, within a budget.
 *
 * @param[in] interval_us   Delay before each status read.
 * @param[in] limit         Maximum number of status reads.
 * @return  true when BSY cleared within the budget.
 */
static bool at25WaitReady(uint32_t interval_us, uint32_t limit)
{
  uint8_t status;
  uint32_t i;

  for (i = 0; i < limit; i++) {
    loaderDelayUs(interval_us);
    if (at25ReadStatus(&status) && (status & AT25XE_FLAGS_SR_WIP) == 0U)
      return true;
  }
  return false;
}

/**
 * @brief   Report whether a sector reads as erased.
 *
 * @details Streams the sector without a buffer, so blank-checking costs no
 *          SRAM against the programmer's transfer buffer.
 *
 * @param[in] offset    Sector-aligned flash offset.
 * @return  true only when every byte read back as 0xFF.
 */
static bool at25SectorBlank(uint32_t offset)
{
  uint32_t i;
  uint8_t value = AT25XE_ERASED_BYTE;
  bool blank = at25BeginAddressed(AT25XE_CMD_READ, offset);

  for (i = 0; blank && i < AT25XE_SECTOR_SIZE; i++) {
    blank = loaderSpiExchange(&loaderFlashBus, 0xFFU, &value) &&
            value == AT25XE_ERASED_BYTE;
  }
  loaderSpiDeselect(&loaderFlashBus);
  return blank;
}

/**
 * @brief   Clear Status Register 1 protection if any is set, and confirm.
 *
 * @details Mirrors the firmware's at25xeUnprotect(): a status read decides
 *          whether a write is needed, and the write uses the Write Status
 *          Register budget. Unlike the firmware, it re-reads the register
 *          and fails if protection is still set -- for example when SRP0/SRP1
 *          and the WP pin lock the status register.
 *
 * @return  true when the array is unprotected by Status Register 1.
 */
static bool at25Unprotect(void)
{
  const uint8_t clear[2] = {AT25XE_CMD_WRITE_STATUS_REG_1, 0x00U};
  uint8_t status;
  bool ok;

  if (!at25ReadStatus(&status))
    return false;
  if ((status & AT25XE_FLAGS_SR_BP) == 0U)
    return true;

  if (!at25Command(AT25XE_CMD_WRITE_ENABLE))
    return false;
  loaderSpiSelect(&loaderFlashBus);
  ok = loaderSpiSend(&loaderFlashBus, clear, sizeof(clear));
  loaderSpiDeselect(&loaderFlashBus);
  if (!ok ||
      !at25WaitReady(AT25XE_WRSR_POLL_INTERVAL_US, AT25XE_WRSR_POLL_LIMIT))
    return false;

  return at25ReadStatus(&status) && (status & AT25XE_FLAGS_SR_BP) == 0U;
}

/* Contract documented in loader_flash.h. */
bool loaderFlashEraseSector(uint32_t offset)
{
  const uint32_t sector = offset - (offset % AT25XE_SECTOR_SIZE);
  const uint32_t erase_interval_us = AT25XE_SECTOR_ERASE_POLL_INTERVAL * 1000U;
  bool ok;

  if (at25SectorBlank(sector))
    return true;

  if (!at25Unprotect())
    return false;

  /* Never issue an erase while a previous operation is still running. */
  if (!at25WaitReady(0U, 1U) &&
      !at25WaitReady(erase_interval_us, AT25XE_SECTOR_ERASE_POLL_LIMIT))
    return false;

  if (!at25Command(AT25XE_CMD_WRITE_ENABLE))
    return false;
  ok = at25BeginAddressed(AT25XE_CMD_SECTOR_ERASE, sector);
  loaderSpiDeselect(&loaderFlashBus);
  if (!ok)
    return false;

  /*
   * BSY clearing is necessary but not sufficient: a protected block clears
   * immediately without erasing. The read-back is what proves the erase.
   */
  return at25WaitReady(erase_interval_us, AT25XE_SECTOR_ERASE_POLL_LIMIT) &&
         at25SectorBlank(sector);
}
/** @brief Page Program granularity: a program must not cross a 256-byte page. */
#define AT25XE_PAGE_SIZE 256U

/**
 * @brief   Compare a flash range with a buffer, streaming without a copy.
 *
 * @return  true only when every byte matches.
 */
static bool at25Matches(uint32_t offset, const uint8_t *buf, uint32_t n)
{
  uint32_t i;
  uint8_t value = 0U;
  bool same = at25BeginAddressed(AT25XE_CMD_READ, offset);

  for (i = 0; same && i < n; i++) {
    same = loaderSpiExchange(&loaderFlashBus, 0xFFU, &value) &&
           value == buf[i];
  }
  loaderSpiDeselect(&loaderFlashBus);
  return same;
}

/* Contract documented in loader_flash.h. */
bool loaderFlashProgram(uint32_t offset, const uint8_t *buf, uint32_t n)
{
  const uint32_t start = offset;
  const uint8_t *const first = buf;
  const uint32_t total = n;
  bool ok;

  if (!at25Unprotect())
    return false;

  while (n > 0U) {
    const uint32_t room = AT25XE_PAGE_SIZE - (offset % AT25XE_PAGE_SIZE);
    const uint32_t chunk = n < room ? n : room;

    if (!at25Command(AT25XE_CMD_WRITE_ENABLE))
      return false;
    ok = at25BeginAddressed(AT25XE_CMD_PAGE_PROG, offset) &&
         loaderSpiSend(&loaderFlashBus, buf, chunk);
    loaderSpiDeselect(&loaderFlashBus);
    if (!ok || !at25WaitReady(AT25XE_PAGE_PROG_POLL_INTERVAL_US,
                              AT25XE_PAGE_PROG_POLL_LIMIT))
      return false;

    offset += chunk;
    buf += chunk;
    n -= chunk;
  }

  return at25Matches(start, first, total);
}
#endif /* LOADER_ALLOW_WRITE */
