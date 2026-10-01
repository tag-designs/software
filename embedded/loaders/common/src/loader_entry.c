/**
 * @file    loader_entry.c
 * @brief   STM32CubeProgrammer external-loader entry points.
 *
 * @details The programmer downloads this image into SRAM and calls these
 *          functions by symbol name. Each returns 1 on success and 0 on
 *          failure. The contract, from ST's reference loaders:
 *
 *          - @c Init() -- prepare the memory for access.
 *          - @c Read(Address, Size, buffer) -- copy @p Size bytes to @p buffer.
 *          - @c Write(Address, Size, buffer) -- program.
 *          - @c SectorErase(StartAddress, EndAddress) -- erase the sectors
 *            containing the range; EndAddress is treated as an address within
 *            the last sector, as ST's loaders do.
 *          - @c MassErase(Parallelism) -- erase everything.
 *
 *          Addresses are in the programmer's view, starting at
 *          LOADER_DEVICE_BASE.
 *
 *          Every entry point re-establishes clock, board, SPI and flash wake
 *          from scratch rather than trusting state left by an earlier call:
 *          the programmer sets SP and PC per call and may reset the core
 *          between calls, and ST's own loaders make the same assumption.
 *          Interrupts are disabled on entry and left disabled: on a hot
 *          attach the tag's interrupts are still armed, and one taken here
 *          would run tag firmware from flash.
 *
 *          Write, SectorErase and MassErase fail in the read-only image,
 *          which is built without the erase and program code
 *          (LOADER_ALLOW_WRITE=0).
 *
 *          @c Serve(buffer, size) is the host library's entry point, outside
 *          the ST set: it initialises once and then runs commands from the
 *          ::loaderService block until told to exit. See loader_service.h.
 */

#include "loader.h"
#include "loader_flash.h"
#include "loader_service.h"

/** @brief Keeps an entry point linked and in the section the map retains. */
#define LOADER_ENTRY __attribute__((used, noinline, section(".loader_entry")))

/** @name Linker-script symbols bounding .bss
 * @{
 */
extern uint32_t __loader_bss_start__; ///< First word of .bss (link map).
extern uint32_t __loader_bss_end__;   ///< One past the last word of .bss.
/** @} */

/**
 * @brief   Establish everything an operation needs, from any prior state.
 *
 * @return  true when the flash answered with its expected identity.
 */
static bool loaderStart(void)
{
  __disable_irq();
  if (!loaderClockInit())
    return false;
  loaderDelayInit();
  if (!loaderBoardInit())
    return false;
  loaderSpiInit(&loaderFlashBus);
  return loaderFlashProbe();
}

/**
 * @brief   Translate and bounds-check a programmer address range.
 *
 * @param[in]  address  Programmer address.
 * @param[in]  size     Byte count.
 * @param[out] offset   Flash offset of @p address.
 * @return  true when [address, address + size) lies within the array.
 */
static bool loaderRange(uint32_t address, uint32_t size, uint32_t *offset)
{
  const uint32_t total = loaderFlashSize();
  uint32_t off;

  if (address < LOADER_DEVICE_BASE)
    return false;
  off = address - LOADER_DEVICE_BASE;
  if (off > total || size > total - off)
    return false;
  *offset = off;
  return true;
}

#if LOADER_ALLOW_WRITE
/**
 * @brief   Erase and verify every sector from @p first to @p last inclusive.
 *
 * @param[in] first  Flash offset within the first sector.
 * @param[in] last   Flash offset within the last sector.
 * @return  true when every sector in the range reads blank.
 */
static bool loaderEraseSectors(uint32_t first, uint32_t last)
{
  const uint32_t sector_size = loaderFlashSectorSize();
  uint32_t offset;

  for (offset = first - (first % sector_size); offset <= last;
       offset += sector_size) {
    if (!loaderFlashEraseSector(offset))
      return false;
  }
  return true;
}
#endif

/**
 * @brief   Prepare the external flash for access.
 *
 * @return  1 when the flash is reachable and identified, 0 otherwise.
 */
LOADER_ENTRY int Init(void)
{
  uint32_t *p;

  for (p = &__loader_bss_start__; p < &__loader_bss_end__; p++)
    *p = 0U;
  return loaderStart() ? 1 : 0;
}

/**
 * @brief   Read from the external flash.
 *
 * @param[in]  Address  Programmer address of the first byte.
 * @param[in]  Size     Byte count.
 * @param[out] buffer   Destination in SRAM, supplied by the programmer.
 * @return  1 on success; 0 when the range is outside the array or a
 *          transfer timed out.
 */
LOADER_ENTRY int Read(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
  uint32_t offset;

  if (!loaderRange(Address, Size, &offset) || !loaderStart())
    return 0;
  return loaderFlashRead(offset, buffer, Size) ? 1 : 0;
}

/**
 * @brief   Program the external flash.
 *
 * @details The target range must already be erased: NOR programming only
 *          clears bits. STM32CubeProgrammer erases the covering sectors
 *          before a write unless told not to.
 *
 * @param[in] Address  Programmer address of the first byte.
 * @param[in] Size     Byte count.
 * @param[in] buffer   Source in SRAM, supplied by the programmer.
 * @return  1 when every byte reads back as written; 0 on a bad range, a
 *          failed program or verify, or in the read-only image.
 */
LOADER_ENTRY int Write(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
#if LOADER_ALLOW_WRITE
  uint32_t offset;

  if (!loaderRange(Address, Size, &offset) || !loaderStart())
    return 0;
  return loaderFlashProgram(offset, buffer, Size) ? 1 : 0;
#else
  (void)Address;
  (void)Size;
  (void)buffer;
  return 0;
#endif
}

/**
 * @brief   Erase the sectors containing a range.
 *
 * @param[in] EraseStartAddress  Programmer address in the first sector.
 * @param[in] EraseEndAddress    Programmer address in the last sector; a
 *                               value below the start erases one sector.
 * @return  1 when every sector in the range reads blank afterwards; 0 on a
 *          bad range, a failed erase, or in the read-only image.
 */
LOADER_ENTRY int SectorErase(uint32_t EraseStartAddress,
                             uint32_t EraseEndAddress)
{
#if LOADER_ALLOW_WRITE
  uint32_t first;
  uint32_t last;

  if (EraseEndAddress < EraseStartAddress)
    EraseEndAddress = EraseStartAddress;
  if (!loaderRange(EraseStartAddress, 1U, &first) ||
      !loaderRange(EraseEndAddress, 1U, &last) || !loaderStart())
    return 0;
  return loaderEraseSectors(first, last) ? 1 : 0;
#else
  (void)EraseStartAddress;
  (void)EraseEndAddress;
  return 0;
#endif
}

/**
 * @brief   Erase the whole external flash.
 *
 * @details Erases sector by sector, skipping sectors that already read
 *          blank, and verifies each. A full erase of a completely written
 *          4 MB part is on the order of the datasheet's 65-75 s chip-erase
 *          time.
 *
 * @param[in] Parallelism  Unused; present for the programmer's signature.
 * @return  1 when the whole array reads blank afterwards; 0 on failure or in
 *          the read-only image.
 */
LOADER_ENTRY int MassErase(uint32_t Parallelism)
{
  (void)Parallelism;
#if LOADER_ALLOW_WRITE
  if (!loaderStart())
    return 0;
  return loaderEraseSectors(0U, loaderFlashSize() - 1U) ? 1 : 0;
#else
  return 0;
#endif
}

/**
 * @brief   The Serve() command block, found by the host through this symbol.
 *
 * @details In .bss, so Serve() clears it on entry; @c magic is written last,
 *          once the part has been probed, so a host that sees the magic sees
 *          a complete block.
 */
volatile LoaderServiceBlock loaderService;

/** @brief STM32L4 IWDG key register; 0xAAAA reloads a running watchdog. */
#define LOADER_IWDG_KR (*(volatile uint32_t *)0x40003000U)
/** @brief Key that reloads the IWDG counter. */
#define LOADER_IWDG_RELOAD 0xAAAAU

/**
 * @brief   Run one command from ::loaderService.
 *
 * @param[in,out] buf   Transfer buffer.
 * @param[in]     size  Buffer length in bytes.
 * @return  A LoaderServiceStatus value.
 */
static int32_t loaderServeCommand(uint8_t *buf, uint32_t size)
{
  const uint32_t cmd = loaderService.cmd;
  const uint32_t offset = loaderService.offset;
  const uint32_t length = loaderService.length;
  const uint32_t total = loaderFlashSize();
  uint32_t jedec;
  uint8_t sr1;

  loaderService.progress = 0U;
  loaderService.detail[LOADER_DETAIL_FAIL_OFFSET] = 0U;

  switch (cmd) {
  case LOADER_CMD_PROBE:
    if (!loaderFlashProbe() || !loaderFlashIdentity(&jedec, &sr1))
      return LOADER_STATUS_IO;
    loaderService.detail[LOADER_DETAIL_JEDEC] = jedec;
    loaderService.detail[LOADER_DETAIL_SR1] = sr1;
    return LOADER_STATUS_OK;

  case LOADER_CMD_READ:
    if (length > size || offset > total || length > total - offset)
      return LOADER_STATUS_RANGE;
    if (!loaderFlashRead(offset, buf, length)) {
      loaderService.detail[LOADER_DETAIL_FAIL_OFFSET] = offset;
      return LOADER_STATUS_IO;
    }
    loaderService.progress = length;
    return LOADER_STATUS_OK;

  case LOADER_CMD_ERASE_SECTOR:
#if LOADER_ALLOW_WRITE
    if (offset >= total)
      return LOADER_STATUS_RANGE;
    if (!loaderFlashEraseSector(offset - (offset % loaderFlashSectorSize()))) {
      loaderService.detail[LOADER_DETAIL_FAIL_OFFSET] = offset;
      return LOADER_STATUS_VERIFY;
    }
    loaderService.progress = loaderFlashSectorSize();
    return LOADER_STATUS_OK;
#else
    return LOADER_STATUS_READ_ONLY;
#endif

  case LOADER_CMD_PROGRAM:
#if LOADER_ALLOW_WRITE
    if (length > size || offset > total || length > total - offset)
      return LOADER_STATUS_RANGE;
    if (!loaderFlashProgram(offset, buf, length)) {
      loaderService.detail[LOADER_DETAIL_FAIL_OFFSET] = offset;
      return LOADER_STATUS_VERIFY;
    }
    loaderService.progress = length;
    return LOADER_STATUS_OK;
#else
    return LOADER_STATUS_READ_ONLY;
#endif

  default:
    return LOADER_STATUS_BAD_COMMAND;
  }
}

/**
 * @brief   Initialise once, then serve host commands until EXIT.
 *
 * @details Clears .bss, brings up clock, board, SPI and the part, records the
 *          part's identity and status register as found in detail[], and
 *          publishes ::LOADER_SERVICE_MAGIC. It then waits for the host to
 *          change @c seq, runs the command, and acknowledges it by copying
 *          @c seq into @c ack. Each command is bounded by the part driver's
 *          own timeouts. The wait reloads the IWDG, which has no effect unless
 *          the hardware watchdog option has started it; the debug freeze
 *          covers only a halted core.
 *
 *          On an initialisation failure the block carries
 *          ::LOADER_STATUS_INIT with the magic, and Serve() returns 0 at once.
 *
 * @param[in,out] buffer  Transfer buffer in SRAM, chosen by the host.
 * @param[in]     size    Its length in bytes.
 * @return  1 after EXIT, 0 when initialisation failed.
 */
LOADER_ENTRY int Serve(uint8_t *buffer, uint32_t size)
{
  uint32_t *p;
  uint32_t jedec = 0U;
  uint8_t sr1 = 0U;
  uint32_t seen;

  for (p = &__loader_bss_start__; p < &__loader_bss_end__; p++)
    *p = 0U;

  loaderService.version = LOADER_SERVICE_VERSION;
  loaderService.detail[LOADER_DETAIL_SIZE] = loaderFlashSize();
  loaderService.detail[LOADER_DETAIL_SECTOR] = loaderFlashSectorSize();
  loaderService.detail[LOADER_DETAIL_WRITABLE] = LOADER_ALLOW_WRITE ? 1U : 0U;

  if (!loaderStart() || !loaderFlashIdentity(&jedec, &sr1)) {
    loaderService.detail[LOADER_DETAIL_JEDEC] = jedec;
    loaderService.status = LOADER_STATUS_INIT;
    __DSB();
    loaderService.magic = LOADER_SERVICE_MAGIC;
    return 0;
  }
  loaderService.detail[LOADER_DETAIL_JEDEC] = jedec;
  loaderService.detail[LOADER_DETAIL_SR1] = sr1;
  loaderService.status = LOADER_STATUS_OK;
  seen = loaderService.seq;
  __DSB();
  loaderService.magic = LOADER_SERVICE_MAGIC;

  for (;;) {
    while (loaderService.seq == seen)
      LOADER_IWDG_KR = LOADER_IWDG_RELOAD;
    seen = loaderService.seq;
    __DSB();
    if (loaderService.cmd == LOADER_CMD_EXIT) {
      loaderService.status = LOADER_STATUS_OK;
      __DSB();
      loaderService.ack = seen;
      return 1;
    }
    loaderService.status = loaderServeCommand(buffer, size);
    __DSB();
    loaderService.ack = seen;
  }
}
