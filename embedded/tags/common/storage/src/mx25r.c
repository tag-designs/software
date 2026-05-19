#include "hal.h"
#include "custom.h"
#include "external_flash.h"
#include "power.h"
#include "rtc_api.h"
#include "storage_device.h"
#include "storage_spi.h"

#define MX25R_SECTOR_SIZE                     (4096)

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

extern void FlashSpiOn(void);
extern void FlashSpiOff(void);

static const TagSpiBus mx25r_spi_bus = {
    .spi = SPI1,
    .cs = LINE_FLASH_nCS,
    .dummy = 0xff,
};

static const TagStorageDevice mx25r_device = {
    .spi = &mx25r_spi_bus,
    .enable = FlashSpiOn,
    .disable = FlashSpiOff,
    .sector_size = MX25R_SECTOR_SIZE,
    .sector_count = EXT_FLASH_SIZE / MX25R_SECTOR_SIZE,
};

static int mx25rSectorSize(const TagStorageDevice *dev)
{
    return dev->sector_size;
}

static int mx25rSectorCount(const TagStorageDevice *dev)
{
    return dev->sector_count;
}

static void mx25rPowerUp(const TagStorageDevice *dev)
{
    tagStorageDeviceEnable(dev);
    //stopMilliseconds(true,1);//chThdSleepMicroseconds(250);
    tagStorageSpiCommand(dev->spi, MX25R_CMD_NOP);
    stopMilliseconds(true,2);//chThdSleepMicroseconds(250);
}

static void mx25rPowerDown(const TagStorageDevice *dev)
{
    tagStorageSpiCommand(dev->spi, MX25R_CMD_DEEP_POWER_DOWN);
    tagStorageDeviceDisable(dev);
}

static uint8_t mx25rStatus(const TagStorageDevice *dev)
{
    uint8_t buf;
    tagStorageSpiCommandReceive(dev->spi, MX25R_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

static int mx25rCheckID(const TagStorageDevice *dev)
{
    uint8_t id[3];
    tagStorageSpiCommandReceive(dev->spi, MX25R_CMD_READ_ID, id, 3);
    if (id[0] != 0xC2)
        return -1;
    if (id[1] != 0x28)
        return -1;
    return (0);
}

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

        tagStorageSpiCommand(dev->spi, MX25R_CMD_WRITE_ENABLE);
        mx25rStatus(dev); // check status after wel -- debug
        tagStorageSpiCommandAddressSend(dev->spi, MX25R_CMD_PAGE_PROG,
                                        address, buf, bytes);
        for (i = 0; i < 12; i++)
        {
            stopMilliseconds(true,1);
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

static bool mx25rSectorErase(const TagStorageDevice *dev, uint32_t address)
{
    uint8_t status;
    int i;

    status = mx25rStatus(dev);
    if (status & (MX25R_FLAGS_SR_WIP))
        return false;
    tagStorageSpiCommand(dev->spi, MX25R_CMD_WRITE_ENABLE);
    tagStorageSpiCommandAddress(dev->spi, MX25R_CMD_SECTOR_ERASE, address);
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

static void mx25rRead(const TagStorageDevice *dev, uint32_t address,
                      uint8_t *buf, int num)
{
    tagStorageSpiCommandAddressReceive(dev->spi, MX25R_CMD_READ, address, buf,
                                       num);
}

int ExSectorSize(void) {
    return mx25rSectorSize(&mx25r_device);
}

int ExSectorCount(void) {
    return mx25rSectorCount(&mx25r_device);
}

void ExFlashPwrUp(void)
{
    mx25rPowerUp(&mx25r_device);
}

void ExFlashPwrDown()
{
    mx25rPowerDown(&mx25r_device);
}

int ExCheckID(void)
{
    return mx25rCheckID(&mx25r_device);
}

bool ExFlashWrite(uint32_t address, uint8_t *buf, int *cnt)
{
    return mx25rWrite(&mx25r_device, address, buf, cnt);
}

bool ExFlashSectorErase(uint32_t address)
{
    return mx25rSectorErase(&mx25r_device, address);
}

void ExFlashRead(uint32_t address, uint8_t *buf, int num)
{
    mx25rRead(&mx25r_device, address, buf, num);
}
