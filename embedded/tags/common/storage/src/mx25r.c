/**
 * @file mx25r.c
 * @brief MX25R external SPI flash command implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "mx25r.h"
#include "storage_device.h"
#include "storage_spi.h"

#define INTER_WRITE_DELAY 2
#define SECTOR_ERASE_POLL_INTERVAL 150

#define MX25R_CMD_NOP             0x00

#define MX25R_CMD_READ            0x03
#define MX25R_CMD_PAGE_PROG       0x02
#define MX25R_CMD_SECTOR_ERASE    0x20
#define MX25R_CMD_READ_ID         0x9F
#define MX25R_CMD_WRITE_ENABLE    0x06
#define MX25R_CMD_READ_STATUS_REG 0x05
#define MX25R_CMD_DEEP_POWER_DOWN 0xB9     // different from AT25

#define MX25R_CMD_RESET_ENABLE    0x66
#define MX25R_CMD_RESET_MEMORY    0x99

// AT25 only #define MX25R_CMD_POWER_UP        0xAB

/* Status Register */

#define MX25R_FLAGS_SR_WIP                    ((uint8_t)0x01)    /* Write in progress */
#define MX25R_FLAGS_SR_WEL                    ((uint8_t)0x02)    /* Write enable latch */
#define MX25R_FLAGS_SR_BP                     ((uint8_t)0x3C)    /* Block protect */
#define MX25R_FLAGS_SR_QE                     ((uint8_t)0x40)    /* Quad enable */
#define MX25R_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /* Status register write disable */

/* Configuration Register 1 */

#define MX25R_FLAGS_CR1_TB                    ((uint8_t)0x08)    /* Top / bottom */

/* Configuration Register 2 */

#define MX25R_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    /* Low power / high performance switch */

/* Security Register */

#define MX25R_FLAGS_SECR_SOI                  ((uint8_t)0x01)    /* Secured OTP indicator */
#define MX25R_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    /* Lock-down secured OTP */
#define MX25R_FLAGS_SECR_PSB                  ((uint8_t)0x04)    /* Program suspend bit */
#define MX25R_FLAGS_SECR_ESB                  ((uint8_t)0x08)    /* Erase suspend bit */
#define MX25R_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    /* Program fail flag */
#define MX25R_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    /* Erase fail flag */

/** @name MX25R storage operations
 * Chip-specific operations behind the generic TagStorageOps table.
 * @{
 */
/**
 * @brief Wake the MX25R and begin its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25rWake(const TagStorageDevice *dev)
{
    tagStorageBusBegin(dev);
    //stopMilliseconds(1);//chThdSleepMicroseconds(250);
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25R_CMD_NOP);
    stopMilliseconds(2);//chThdSleepMicroseconds(250);
}

/**
 * @brief Enter MX25R deep power-down and end its storage bus session.
 *
 * @param[in] dev Storage device descriptor.
 */
static void mx25rSleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25R_CMD_DEEP_POWER_DOWN);
    tagStorageBusEnd(dev);
}

/**
 * @brief Read the MX25R status register.
 *
 * @param[in] dev Storage device descriptor.
 * @return Raw status register value.
 */
static uint8_t mx25rStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), MX25R_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

/**
 * @brief Verify the MX25R JEDEC identity.
 *
 * @param[in] dev Storage device descriptor.
 * @return 0 on expected identity, or -1 on mismatch.
 */
static int mx25rCheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    tagStorageSpiCommandReceive(tagStorageSpiDevice(dev), MX25R_CMD_READ_ID, id, 3);
    if (id[0] != 0xC2)
        return -1;
    if (id[1] != 0x28)
        return -1;
    return (0);
}

/**
 * @brief Program bytes to MX25R flash across page boundaries.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to program.
 * @param[in] buf Source buffer.
 * @param[in,out] cnt Requested byte count on entry, programmed byte count on return.
 * @return true when all requested bytes were programmed.
 */
static bool mx25rWrite(const TagStorageDevice *dev, uint32_t address,
                       uint8_t *buf, int *cnt)
{
    int num = *cnt;
    int i;
    *cnt = 0;
    while (num)
    {
        int max = 256 - address%256;
        int bytes = num > max ? max : num; 

        tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25R_CMD_WRITE_ENABLE);
        mx25rStatus(dev); // check status after wel -- debug
        tagStorageSpiCommandAddressSend(tagStorageSpiDevice(dev), MX25R_CMD_PAGE_PROG,
                                        address, buf, bytes);
        for (i = 0; i < 12; i++)
        {
            stopMilliseconds(1);
            uint8_t status = mx25rStatus(dev);
            if ((status & MX25R_FLAGS_SR_WIP) == 0)
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
 * @brief Erase one MX25R sector and wait for completion.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Address within the sector to erase.
 * @return true when the erase completed.
 */
static bool mx25rSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    status = mx25rStatus(dev);
    if (status & (MX25R_FLAGS_SR_WIP))
        return false;
    tagStorageSpiCommand(tagStorageSpiDevice(dev), MX25R_CMD_WRITE_ENABLE);
    tagStorageSpiCommandAddress(tagStorageSpiDevice(dev), MX25R_CMD_SECTOR_ERASE, address);
    for (i = 0; i < 5; i++)
    {
        chThdSleepMilliseconds(SECTOR_ERASE_POLL_INTERVAL);
        status = mx25rStatus(dev);
        if (!(status & MX25R_FLAGS_SR_WIP))
            break;
    }
    if (i == 5)
    {
        return false;
    }
    return true;
}

/**
 * @brief Read bytes from MX25R flash.
 *
 * @param[in] dev Storage device descriptor.
 * @param[in] address Flash byte address to read.
 * @param[out] buf Destination buffer.
 * @param[in] num Number of bytes to read.
 */
static void mx25rRead(const TagStorageDevice *dev, uint32_t address,
                      uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(tagStorageSpiDevice(dev), MX25R_CMD_READ, address, buf,
                                       num);
}

/** MX25R operation table consumed by the generic storage layer. */
const TagStorageOps mx25rStorageOps = {
    .wake = mx25rWake,
    .sleep = mx25rSleep,
    .check_id = mx25rCheckID,
    .write = mx25rWrite,
    .sector_erase = mx25rSectorErase,
    .read = mx25rRead,
};
/** @} */
