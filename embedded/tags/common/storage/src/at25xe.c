/**
 * @file at25xe.c
 * @brief AT25XE external SPI flash command implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "at25xe.h"
#include "storage_device.h"
#include "storage_spi.h"
#include "phase_probe.h"

#define INTER_WRITE_DELAY 2
#define PAGE_PROG_POLL_INTERVAL_US 100
#define PAGE_PROG_POLL_LIMIT 120
/*
 * Write Status Register's own write cycle (tW) was measured taking up to
 * ~11.4 ms (114 iterations at PAGE_PROG_POLL_INTERVAL_US), right at the edge
 * of the page-program budget above -- occasionally over it, at which point
 * at25xeUnprotect() timed out and returned with WIP still genuinely set on
 * the part. Give it its own, more generous budget.
 */
#define WRSR_POLL_INTERVAL_US 200
#define WRSR_POLL_LIMIT 250
#define SECTOR_ERASE_POLL_INTERVAL 150
/*
 * The original 5-iteration budget (750 ms) left a residual ~4% failure rate
 * under repeated testing -- occasional sector erases genuinely take longer.
 * Wake and erase are rare events (once per checkpoint), so a larger budget
 * costs nothing in the common case and avoids silently dropping a write.
 */
#define SECTOR_ERASE_POLL_LIMIT 20

#define AT25XE_CMD_READ            0x03
#define AT25XE_CMD_PAGE_PROG       0x02
#define AT25XE_CMD_SECTOR_ERASE    0x20
#define AT25XE_CMD_READ_ID         0x9F
#define AT25XE_CMD_WRITE_ENABLE    0x06
#define AT25XE_CMD_READ_STATUS_REG 0x05
#define AT25XE_CMD_WRITE_STATUS_REG_1 0x01
#define AT25XE_CMD_DEEP_POWER_DOWN 0xB9
#define AT25XE_CMD_ULTRA_DEEP_POWER_DOWN 0x79
#define AT25XE_CMD_POWER_UP        0xAB
#define AT25XE_CMD_RESET_ENABLE    0x66
#define AT25XE_CMD_RESET_MEMORY    0x99

/* Status Register */

#define AT25XE_FLAGS_SR_WIP                    ((uint8_t)0x01)    /* Write in progress */
#define AT25XE_FLAGS_SR_WEL                    ((uint8_t)0x02)    /* Write enable latch */
#define AT25XE_FLAGS_SR_BP                     ((uint8_t)0x3C)    /* Block protect */
#define AT25XE_FLAGS_SR_QE                     ((uint8_t)0x40)    /* Quad enable */
#define AT25XE_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /* Status register write disable */

/* Configuration Register 1 */

#define AT25XE_FLAGS_CR1_TB                    ((uint8_t)0x08)    /* Top / bottom */

/* Configuration Register 2 */

#define AT25XE_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    /* Low power / high performance switch */

/* Security Register */

#define AT25XE_FLAGS_SECR_SOI                  ((uint8_t)0x01)    /* Secured OTP indicator */
#define AT25XE_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    /* Lock-down secured OTP */
#define AT25XE_FLAGS_SECR_PSB                  ((uint8_t)0x04)    /* Program suspend bit */
#define AT25XE_FLAGS_SECR_ESB                  ((uint8_t)0x08)    /* Erase suspend bit */
#define AT25XE_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    /* Program fail flag */
#define AT25XE_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    /* Erase fail flag */

/** @name AT25XE storage operations
 * Chip-specific operations behind the generic TagStorageOps table.
 * @{
 */
static bool at25xeUnprotect(const TagStorageDevice *dev);
static uint8_t at25xeStatus(const TagStorageDevice *dev);

/**
 * @brief Wake the AT25XE and begin its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void at25xeWake(const TagStorageDevice *dev)
{
    tagStorageBusBegin(dev);
    //stopMilliseconds(1);//chThdSleepMicroseconds(250);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), AT25XE_CMD_POWER_UP);
    stopMilliseconds(2);//chThdSleepMicroseconds(250);
    tagPhaseProbeMark(7);   /* flash awake after power-up delay */

    /*
     * Ensure the array is writable every wake. See at25xeUnprotect()'s doc
     * comment: a protected array silently no-ops Program/Erase commands
     * (no error, busy bit never asserts), and protection can be
     * non-volatile, so this is not a one-time fix -- verify it on every
     * wake rather than assuming a prior wake's write already cleared it.
     */
    at25xeUnprotect(dev);
}

/**
 * @brief Enter AT25XE deep power-down and end its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void at25xeSleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(tagStorageSpiDevice(dev), AT25XE_CMD_DEEP_POWER_DOWN);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), AT25XE_CMD_ULTRA_DEEP_POWER_DOWN);
    tagStorageBusEnd(dev);
    tagPhaseProbeMark(10);  /* flash in ultra-deep power-down, bus released */
}

/**
 * @brief Read the AT25XE status register.
 *
 * @param[in] dev Storage device descriptor.
 * @return Raw status register value.
 */
static uint8_t at25xeStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), AT25XE_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

/**
 * @brief Clear AT25XE Status Register 1 so the array is fully writable.
 *
 * @details The Block Protect field (BP[2:0], bits 4:2 of Status Register 1)
 *          can protect part or all of the array from Program and Erase
 *          commands; when a block is protected, those commands are silently
 *          not executed -- the device just returns to idle with no error and
 *          the busy bit never asserts, so a caller polling for completion
 *          sees an immediate, spurious "success". Status Register 1 can be
 *          held in non-volatile storage, so a protected state can persist
 *          across power cycles and MCU reflashes. The factory default is
 *          BP[2:0]=000 (unprotected), but nothing in this driver previously
 *          verified or restored that, so any accidental protection would go
 *          unnoticed indefinitely. Writing SR1=0x00 clears BPSIZE/TB/BP[2:0]
 *          unconditionally; this is a no-op (and harmless) when the array is
 *          already unprotected.
 *
 * @param[in] dev Storage device descriptor.
 * @return true when the write-enable and write-status-register transactions
 *         completed and WIP cleared before the write cycle timeout.
 *
 * @note    A Write Status Register command asserts WIP for its own write
 *          cycle (tW), just like Program and Erase. This function used to
 *          return as soon as the command was clocked out, without waiting
 *          for that cycle to finish -- since it runs on every wake, the very
 *          next command (typically a sector erase, moments later) would see
 *          WIP still set from this write and read it as "flash stuck busy",
 *          failing immediately via its own early-exit guard. Poll here so
 *          callers never observe this write's WIP window.
 */
static bool at25xeUnprotect(const TagStorageDevice *dev)
{
    const TagSpiDevice *spi = tagStorageSpiDevice(dev);
    uint8_t header[2] = { AT25XE_CMD_WRITE_STATUS_REG_1, 0x00U };
    bool ok;
    int i;

    tagStorageSpiCommand(spi, AT25XE_CMD_WRITE_ENABLE);
    tagSpiSelect(spi);
    ok = tagStorageSpiWrite(spi, header, sizeof(header));
    tagSpiDeselect(spi);
    if (!ok)
        return false;

    for (i = 0; i < WRSR_POLL_LIMIT; i++) {
        chThdSleepMicroseconds(WRSR_POLL_INTERVAL_US);
        if ((at25xeStatus(dev) & AT25XE_FLAGS_SR_WIP) == 0)
            break;
    }
    return i < WRSR_POLL_LIMIT;
}

/**
 * @brief Verify the AT25XE JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return Detected flash size in bytes on success, or -1 on mismatch.
 */
static int at25xeCheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), AT25XE_CMD_READ_ID, id, 3);
    if (id[0] != 0x1F)
        return -1;
    if (id[1] != 0x47)
        return -1;
    return (1<<((id[1]& 0x1f)+9));
}

/**
 * @brief Program bytes to AT25XE flash across page boundaries.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to program.
 * @param[in] buf Source buffer.
 * @param[in,out] cnt Requested byte count on entry, programmed byte count on return.
 * @return true when all requested bytes were programmed.
 */
static bool at25xeWrite(const TagStorageDevice *dev, uint32_t address,
                        uint8_t *buf, int *cnt)
{
    int num = *cnt;
    int i;
    *cnt = 0;
    while (num)
    {
        int max = 256 - address%256;
        int bytes = num > max ? max : num; 

        tagStorageSpiCommand(tagStorageSpiDevice(dev), AT25XE_CMD_WRITE_ENABLE);
        at25xeStatus(dev); // check status after wel -- debug
        tagStorageSpiCommandAddressSend(tagStorageSpiDevice(dev), AT25XE_CMD_PAGE_PROG,
                                        address, buf, bytes);
        tagPhaseProbeMark(8);   /* PAGE_PROG issued, array programming */
        for (i = 0; i < PAGE_PROG_POLL_LIMIT; i++)
        {
            chThdSleepMicroseconds(PAGE_PROG_POLL_INTERVAL_US);
            uint8_t status = at25xeStatus(dev);
            if ((status & AT25XE_FLAGS_SR_WIP) == 0)
                break;
        } 
        tagPhaseProbeMark(9);   /* WIP cleared */
        tagPhaseProbeAux(0, (uint32_t)i);  /* poll iterations at 100 us */
        if (i == PAGE_PROG_POLL_LIMIT)
            return false;
        address += bytes;
        buf += bytes;
        num -= bytes;
        *cnt += bytes;
    }
    return true;
}

/**
 * @brief Erase one AT25XE sector and wait for completion.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
static bool at25xeSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    status = at25xeStatus(dev);
    if (status & (AT25XE_FLAGS_SR_WIP))
        return false;
    tagStorageSpiCommand(tagStorageSpiDevice(dev), AT25XE_CMD_WRITE_ENABLE);
    tagStorageSpiCommandAddress(tagStorageSpiDevice(dev), AT25XE_CMD_SECTOR_ERASE, address);
    for (i = 0; i < SECTOR_ERASE_POLL_LIMIT; i++)
    {
        chThdSleepMilliseconds(SECTOR_ERASE_POLL_INTERVAL);
        status = at25xeStatus(dev);
        if (!(status & AT25XE_FLAGS_SR_WIP))
            break;
    }
    if (i == SECTOR_ERASE_POLL_LIMIT)
    {
        return false;
    }
    return true;
}

/**
 * @brief Read bytes from AT25XE flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
static void at25xeRead(const TagStorageDevice *dev, uint32_t address,
                       uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(tagStorageSpiDevice(dev), AT25XE_CMD_READ, address, buf,
                                       num);
}

/** AT25XE operation table consumed by the generic storage layer. */
const TagStorageOps at25xeStorageOps = {
    .wake = at25xeWake,
    .sleep = at25xeSleep,
    .check_id = at25xeCheckID,
    .write = at25xeWrite,
    .sector_erase = at25xeSectorErase,
    .read = at25xeRead,
};
/** @} */
