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

static void mx25lWake(const TagStorageDevice *dev)
{
    tagStorageBusBegin(dev);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_POWER_UP);
    chThdSleepMicroseconds(PWR_UP_DELAY_US);
}

static void mx25lSleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25L_CMD_DEEP_POWER_DOWN);
    tagStorageBusEnd(dev);
}

static uint8_t mx25lStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), MX25L_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

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

static void mx25lRead(const TagStorageDevice *dev, uint32_t address,
                      uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(tagStorageSpiDevice(dev), MX25L_CMD_READ, address, buf,
                                       num);
}

const TagStorageOps mx25lStorageOps = {
    .wake = mx25lWake,
    .sleep = mx25lSleep,
    .check_id = mx25lCheckID,
    .write = mx25lWrite,
    .sector_erase = mx25lSectorErase,
    .read = mx25lRead,
};
