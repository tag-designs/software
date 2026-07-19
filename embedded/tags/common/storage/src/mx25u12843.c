/**
 * @file mx25u12843.c
 * @brief MX25U12843 1.8 V external SPI flash command implementation.
 * @author tag firmware authors
 * @date 2026-07-07
 */

#include "hal.h"
#include "core_types.h"
#include "custom.h"
#include "debug_log.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_mx25u12843.h"
#include "storage_spi.h"

#define PWR_UP_DELAY_US 20
#define PAGE_PROG_POLL_INTERVAL_US 100
#define PAGE_PROG_POLL_LIMIT 120
#define SECTOR_ERASE_POLL_INTERVAL 150

#define MX25U12843_CMD_READ            0x03
#define MX25U12843_CMD_PAGE_PROG       0x02
#define MX25U12843_CMD_SECTOR_ERASE    0x20
#define MX25U12843_CMD_READ_ID         0x9F
#define MX25U12843_CMD_WRITE_ENABLE    0x06
#define MX25U12843_CMD_READ_STATUS_REG 0x05
#define MX25U12843_CMD_DEEP_POWER_DOWN 0xB9
#define MX25U12843_CMD_POWER_UP        0xAB

#define MX25U12843_FLAGS_SR_WIP        ((uint8_t)0x01)

#define MX25U12843_ID_MANUFACTURER     0xC2
#define MX25U12843_ID_MEMORY_TYPE      0x25
#define MX25U12843_ID_MEMORY_DENSITY   0x38

/** @name MX25U12843 storage operations
 * Chip-specific operations behind the generic TagStorageOps table.
 * @{
 */
/**
 * @brief Wake the MX25U12843 and begin its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25u12843Wake(const TagStorageDevice *dev)
{
    tagStorageBusBegin(dev);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25U12843_CMD_POWER_UP);
    chThdSleepMicroseconds(PWR_UP_DELAY_US);
}

/**
 * @brief Enter MX25U12843 deep power-down and end its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25u12843Sleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25U12843_CMD_DEEP_POWER_DOWN);
    tagStorageBusEnd(dev);
}

/**
 * @brief Read the MX25U12843 status register.
 *
 * @param[in] dev Storage device descriptor.
 * @param[out] status Raw status register value.
 * @return true when the status byte was read.
 */
static bool mx25u12843Status(const TagStorageDevice *dev, uint8_t *status)
{
    uint8_t buf;
    if (!tagStorageSpiCommandReceive(tagStorageSpiDevice(dev),
                                     MX25U12843_CMD_READ_STATUS_REG, &buf, 1))
        return false;
    *status = buf;
    return true;
}

/**
 * @brief Verify the MX25U12843 JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return 0 on expected identity, or -1 on mismatch.
 */
static int mx25u12843CheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    if (!tagStorageSpiCommandReceive(tagStorageSpiDevice(dev),
                                     MX25U12843_CMD_READ_ID, id, 3))
        return -1;
    if (MONCONNECTED)
    {
        debug_log_printf("Flash: MID 0x%x DID 0x%x SIZE 0x%x",
                         id[0], id[1], id[2]);
    }
    if (id[0] != MX25U12843_ID_MANUFACTURER)
        return -1;
    if (id[1] != MX25U12843_ID_MEMORY_TYPE)
        return -1;
    if (id[2] != MX25U12843_ID_MEMORY_DENSITY)
        return -1;
    return 0;
}

/**
 * @brief Program bytes to MX25U12843 flash across page boundaries.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to program.
 * @param[in] buf Source buffer.
 * @param[in,out] cnt Requested byte count on entry, programmed byte count on return.
 * @return true when all requested bytes were programmed.
 */
static bool mx25u12843Write(const TagStorageDevice *dev, uint32_t address,
                            uint8_t *buf, int *cnt)
{
    int num = *cnt;
    int i;
    *cnt = 0;
    while (num)
    {
        int max = 256 - address % 256;
        int bytes = num > max ? max : num;

        uint8_t status;
        if (!tagStorageSpiCommand(tagStorageSpiDevice(dev),
                                  MX25U12843_CMD_WRITE_ENABLE))
            return false;
        if (!mx25u12843Status(dev, &status))
            return false;
        if (!tagStorageSpiCommandAddressSend(tagStorageSpiDevice(dev),
                                             MX25U12843_CMD_PAGE_PROG, address,
                                             buf, bytes))
            return false;
        for (i = 0; i < PAGE_PROG_POLL_LIMIT; i++)
        {
            chThdSleepMicroseconds(PAGE_PROG_POLL_INTERVAL_US);
            if (!mx25u12843Status(dev, &status))
                return false;
            if ((status & MX25U12843_FLAGS_SR_WIP) == 0)
                break;
        }
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
 * @brief Erase one MX25U12843 sector and wait for completion.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
static bool mx25u12843SectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    if (!mx25u12843Status(dev, &status))
        return false;
    if (status & MX25U12843_FLAGS_SR_WIP)
        return false;
    if (!tagStorageSpiCommand(tagStorageSpiDevice(dev),
                              MX25U12843_CMD_WRITE_ENABLE))
        return false;
    if (!tagStorageSpiCommandAddress(tagStorageSpiDevice(dev),
                                     MX25U12843_CMD_SECTOR_ERASE, address))
        return false;
    for (i = 0; i < 5; i++)
    {
        chThdSleepMilliseconds(SECTOR_ERASE_POLL_INTERVAL);
        if (!mx25u12843Status(dev, &status))
            return false;
        if (!(status & MX25U12843_FLAGS_SR_WIP))
            break;
    }
    if (i == 5)
    {
        return false;
    }
    return true;
}

/**
 * @brief Read bytes from MX25U12843 flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
static void mx25u12843Read(const TagStorageDevice *dev, uint32_t address,
                           uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(tagStorageSpiDevice(dev),
                                       MX25U12843_CMD_READ, address, buf, num);
}

/** MX25U12843 operation table consumed by the generic storage layer. */
const TagStorageOps mx25u12843StorageOps = {
    .wake = mx25u12843Wake,
    .sleep = mx25u12843Sleep,
    .check_id = mx25u12843CheckID,
    .write = mx25u12843Write,
    .sector_erase = mx25u12843SectorErase,
    .read = mx25u12843Read,
};
/** @} */
