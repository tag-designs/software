#include "hal.h"
#include "custom.h"
#include "power.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_spi.h"

#define AT25XE_SECTOR_SIZE                     (4096)

#define INTER_WRITE_DELAY 2
#define SECTOR_ERASE_POLL_INTERVAL 150

#define AT25XE_CMD_READ            0x03
#define AT25XE_CMD_PAGE_PROG       0x02
#define AT25XE_CMD_SECTOR_ERASE    0x20
#define AT25XE_CMD_READ_ID         0x9F
#define AT25XE_CMD_WRITE_ENABLE    0x06
#define AT25XE_CMD_READ_STATUS_REG 0x05
#define AT25XE_CMD_DEEP_POWER_DOWN 0x79
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

extern void FlashSpiOn(void);
extern void FlashSpiOff(void);

static const TagSpiBus at25xe_spi_bus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .dummy = 0xff,
};

static void at25xeWake(const TagStorageDevice *dev)
{
    tagStorageDeviceEnable(dev);
    //stopMilliseconds(true,1);//chThdSleepMicroseconds(250);
    tagStorageSpiCommand(dev->spi, AT25XE_CMD_POWER_UP);
    stopMilliseconds(true,2);//chThdSleepMicroseconds(250);
}

static void at25xeSleep(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(dev->spi, AT25XE_CMD_DEEP_POWER_DOWN);
    tagStorageDeviceDisable(dev);
}

static uint8_t at25xeStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(dev->spi, AT25XE_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

static int at25xeCheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    tagStorageSpiCommandReceive(dev->spi, AT25XE_CMD_READ_ID, id, 3);
    if (id[0] != 0x1F)
        return -1;
    if (id[1] != 0x47)
        return -1;
    return (1<<((id[1]& 0x1f)+9));
}

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

        tagStorageSpiCommand(dev->spi, AT25XE_CMD_WRITE_ENABLE);
        at25xeStatus(dev); // check status after wel -- debug
        tagStorageSpiCommandAddressSend(dev->spi, AT25XE_CMD_PAGE_PROG,
                                        address, buf, bytes);
        for (i = 0; i < 12; i++)
        {
            stopMilliseconds(true,1);
            uint8_t status = at25xeStatus(dev);
            if ((status & AT25XE_FLAGS_SR_WIP) == 0)
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

static bool at25xeSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    status = at25xeStatus(dev);
    if (status & (AT25XE_FLAGS_SR_WIP))
        return false;
    tagStorageSpiCommand(dev->spi, AT25XE_CMD_WRITE_ENABLE);
    tagStorageSpiCommandAddress(dev->spi, AT25XE_CMD_SECTOR_ERASE, address);
    for (i = 0; i < 5; i++)
    {
        chThdSleepMilliseconds(SECTOR_ERASE_POLL_INTERVAL);
        status = at25xeStatus(dev);
        if (!(status & AT25XE_FLAGS_SR_WIP))
            break;
    }
    if (i == 5)
    {
        return false;
    }
    return true;
}

static void at25xeRead(const TagStorageDevice *dev, uint32_t address,
                       uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(dev->spi, AT25XE_CMD_READ, address, buf,
                                       num);
}

static const TagStorageOps at25xe_ops = {
    .wake = at25xeWake,
    .sleep = at25xeSleep,
    .check_id = at25xeCheckID,
    .write = at25xeWrite,
    .sector_erase = at25xeSectorErase,
    .read = at25xeRead,
};

const TagStorageDevice tagExternalFlash = {
    .ops = &at25xe_ops,
    .spi = &at25xe_spi_bus,
    .enable = FlashSpiOn,
    .disable = FlashSpiOff,
    .sector_size = AT25XE_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / AT25XE_SECTOR_SIZE,
};
