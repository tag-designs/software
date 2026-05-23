/**
 * @file mx25l.c
 * @brief MX25L external SPI flash command implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "core_types.h"
#include "custom.h"
#include "debug_log.h"
#include "storage_mx25l.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_spi.h"

#define PWR_UP_DELAY_US 20
#define SECTOR_ERASE_POLL_INTERVAL 150

#define MX25L_CMD_READ            0x03
#define MX25L_CMD_PAGE_PROG       0x02
#define MX25L_CMD_SECTOR_ERASE    0x20
#define MX25L_CMD_READ_ID         0x9F
#define MX25L_CMD_WRITE_ENABLE    0x06
#define MX25L_CMD_READ_STATUS_REG 0x05
#define MX25L_CMD_DEEP_POWER_DOWN 0xB9
#define MX25L_CMD_POWER_UP        0xAB

#define MX25L_FLAGS_SR_WIP        ((uint8_t)0x01)

/** @name MX25L storage operations
 * Chip-specific operations behind the generic TagStorageOps table.
 * @{
 */
/**
 * @brief Wake the MX25L and begin its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25lWake(const TagStorageDevice *dev)
{
    tagStorageBusBegin(dev);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_POWER_UP);
    chThdSleepMicroseconds(PWR_UP_DELAY_US);
}

/**
 * @brief Enter MX25L deep power-down and end its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25lSleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_DEEP_POWER_DOWN);
    tagStorageBusEnd(dev);
}

/**
 * @brief Read the MX25L status register.
 *
 * @param[in] dev Storage device descriptor.
 * @return Raw status register value.
 */
static uint8_t mx25lStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), MX25L_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

/**
 * @brief Verify the MX25L JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return 0 on expected identity, or -1 on mismatch.
 */
static int mx25lCheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), MX25L_CMD_READ_ID, id, 3);
    if (MONCONNECTED)
    {
        debug_log_printf("Flash: MID 0x%x DID 0x%x SIZE 0x%x",
                         id[0], id[1], id[2]);
    }
    if (id[0] != 0xC2)
        return -1;
    if (id[1] != 0x20)
        return -1;
    if (id[2] != 0x18)
        return -1;
    return 0;
}

/**
 * @brief Program bytes to MX25L flash across page boundaries.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to program.
 * @param[in] buf Source buffer.
 * @param[in,out] cnt Requested byte count on entry, programmed byte count on return.
 * @return true when all requested bytes were programmed.
 */
static bool mx25lWrite(const TagStorageDevice *dev, uint32_t address,
                       uint8_t *buf, int *cnt)
{
    int num = *cnt;
    int i;
    *cnt = 0;
    while (num)
    {
        int max = 256 - address % 256;
        int bytes = num > max ? max : num;

        tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_WRITE_ENABLE);
        mx25lStatus(dev);
        tagStorageSpiCommandAddressSend(tagStorageSpiDevice(dev), MX25L_CMD_PAGE_PROG,
                                        address, buf, bytes);
        for (i = 0; i < 12; i++)
        {
            stopMilliseconds(1);
            uint8_t status = mx25lStatus(dev);
            if ((status & MX25L_FLAGS_SR_WIP) == 0)
                break;
        }
        if (i == 12)
            return false;
        address += bytes;
        buf += bytes;
        num -= bytes;
        *cnt += bytes;
    }
    return true;
}

/**
 * @brief Erase one MX25L sector and wait for completion.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
static bool mx25lSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    status = mx25lStatus(dev);
    if (status & MX25L_FLAGS_SR_WIP)
        return false;
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_WRITE_ENABLE);
    tagStorageSpiCommandAddress(tagStorageSpiDevice(dev), MX25L_CMD_SECTOR_ERASE, address);
    for (i = 0; i < 5; i++)
    {
        chThdSleepMilliseconds(SECTOR_ERASE_POLL_INTERVAL);
        status = mx25lStatus(dev);
        if (!(status & MX25L_FLAGS_SR_WIP))
            break;
    }
    if (i == 5)
    {
        return false;
    }
    return true;
}

/**
 * @brief Read bytes from MX25L flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
static void mx25lRead(const TagStorageDevice *dev, uint32_t address,
                      uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(tagStorageSpiDevice(dev), MX25L_CMD_READ, address, buf,
                                       num);
}

/** MX25L operation table consumed by the generic storage layer. */
const TagStorageOps mx25lStorageOps = {
    .wake = mx25lWake,
    .sleep = mx25lSleep,
    .check_id = mx25lCheckID,
    .write = mx25lWrite,
    .sector_erase = mx25lSectorErase,
    .read = mx25lRead,
};
/** @} */
