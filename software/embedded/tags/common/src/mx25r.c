#include "hal.h"
#include "app.h"
#include "external_flash.h"

#define MX25R_CMD_READ 0x03
#define MX25R_CMD_PAGE_PROG 0x02
#define MX25R_CMD_SECTOR_ERASE 0x20
#define MX25R_CMD_READ_ID 0x9F
#define MX25R_CMD_WRITE_ENABLE 0x06
#define MX25R_CMD_READ_STATUS_REG 0x05
#define MX25R_CMD_READ_CFG_REG 0x15
#define MX25R_CMD_WRITE_STATUS_CFG_REG 0x01
#define MX25R_CMD_READ_SEC_REG 0x2B
#define MX25R_CMD_DEEP_POWER_DOWN 0xB9
#define MX25R_CMD_RESET_ENABLE 0x66
#define MX25R_CMD_RESET_MEMORY 0x99

#define MX25R_MANUFACTURE_ID ((uint8_t)0xC2)
#define MX25R_MEMORY_TYPE_ID ((uint8_t)0x28)
#define MX25R_SECTOR_SIZE (4096)

/* Status Register */

#define MX25R_FLAGS_SR_WIP ((uint8_t)0x01)  /* Write in progress */
#define MX25R_FLAGS_SR_WEL ((uint8_t)0x02)  /* Write enable latch */
#define MX25R_FLAGS_SR_BP ((uint8_t)0x3C)   /* Block protect */
#define MX25R_FLAGS_SR_QE ((uint8_t)0x40)   /* Quad enable */
#define MX25R_FLAGS_SR_SRWD ((uint8_t)0x80) /* Status register write disable */

/* Configuration Register 1 */

#define MX25R_FLAGS_CR1_TB ((uint8_t)0x08) /* Top / bottom */

/* Configuration Register 2 */

#define MX25R_FLAGS_CR2_LH_SWITCH ((uint8_t)0x02) /* Low power / high performance switch */

/* Security Register */

#define MX25R_FLAGS_SECR_SOI ((uint8_t)0x01)    /* Secured OTP indicator */
#define MX25R_FLAGS_SECR_LDSO ((uint8_t)0x02)   /* Lock-down secured OTP */
#define MX25R_FLAGS_SECR_PSB ((uint8_t)0x04)    /* Program suspend bit */
#define MX25R_FLAGS_SECR_ESB ((uint8_t)0x08)    /* Erase suspend bit */
#define MX25R_FLAGS_SECR_P_FAIL ((uint8_t)0x20) /* Program fail flag */
#define MX25R_FLAGS_SECR_E_FAIL ((uint8_t)0x40) /* Erase fail flag */

extern void FlashSpiOn(void);
extern void FlashSpiOff(void);

static void spiSendPolled(uint32_t n, uint8_t *buf)
{
    volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
    while (n--)
    {
        *spidr = *buf++;
        while ((SPI1->SR & SPI_SR_RXNE) == 0)
            ;
        *spidr;
    }
}

static void spiReceivePolled(uint32_t n, uint8_t *buf)
{
    volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
    while (n--)
    {
        *spidr = 0xff;
        while ((SPI1->SR & SPI_SR_RXNE) == 0)
            ;
        *buf++ = *spidr;
    }
}

static void spi_addr(uint32_t address)
{
    uint8_t buf[3];
    buf[0] = address >> 16;
    buf[1] = address >> 8;
    buf[2] = address & 0xff;
    spiSendPolled(3, buf);
}

static void spi_cmd(uint8_t cmd)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    palSetLine(LINE_FLASH_nCS);
}

static void spi_cmd_addr(uint8_t cmd, uint32_t address)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    spi_addr(address);
    palSetLine(LINE_FLASH_nCS);
}

static void spi_cmd_rcv(uint8_t cmd, uint8_t *buf, int num)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    spiReceivePolled(num, buf);
    palSetLine(LINE_FLASH_nCS);
}

static void spi_cmd_addr_rcv(uint8_t cmd, uint32_t address, uint8_t *buf, int num)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    spi_addr(address);
    spiReceivePolled(num, buf);
    palSetLine(LINE_FLASH_nCS);
}

static void spi_cmd_snd(uint8_t cmd, uint8_t *buf, int num)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    spiSendPolled(num, buf);
    palSetLine(LINE_FLASH_nCS);
}

static void spi_cmd_addr_snd(uint8_t cmd, uint32_t address, uint8_t *buf, int num)
{
    palClearLine(LINE_FLASH_nCS);
    spiSendPolled(1, &cmd);
    spi_addr(address);
    spiSendPolled(num, buf);
    palSetLine(LINE_FLASH_nCS);
}

void ExFlashPwrUp(void)
{
    FlashSpiOn();
    palClearLine(LINE_FLASH_nCS);
    palSetLine(LINE_FLASH_nCS);
    chThdSleepMicroseconds(20);
}

void ExFlashPwrDown(void)
{
    spi_cmd(MX25R_CMD_DEEP_POWER_DOWN);
    FlashSpiOff();
}

static uint8_t mx25r_Status(void)
{
    uint8_t buf;
    spi_cmd_rcv(MX25R_CMD_READ_STATUS_REG, &buf, 1);
    return buf;
}

static void mx25r_ReadID(uint8_t *id)
{
    spi_cmd_rcv(MX25R_CMD_READ_ID, id, 3);
}

int ExSectorSize(void){
    return 4096;
}

int ExCheckID(void){
    uint8_t id[3];
    mx25r_ReadID(id);
    if (id[0] != MX25R_MANUFACTURE_ID) 
       return -1;
    if (id[1] != MX25R_MEMORY_TYPE_ID)
        return -1;
    return 1<<(id[2]-3); 
}
    
static uint8_t mx25r_ReadSCR(void)
{
    uint8_t buf;
    spi_cmd_rcv(MX25R_CMD_READ_SEC_REG, &buf, 1);
    return buf;
}

uint16_t mx25r_ReadCFG(void)
{
    uint16_t buf;
    spi_cmd_rcv(MX25R_CMD_READ_CFG_REG, (uint8_t *)&buf, 2);
    return buf;
}

bool ExFlashWrite(uint32_t address, uint8_t *buf, int *cnt)
{
    int num = *cnt;
    *cnt = 0;

    while (num)
    {
        int bytes = num > 4 ? 4 : num;
        int i;

        spi_cmd(MX25R_CMD_WRITE_ENABLE);
        spi_cmd_addr_snd(MX25R_CMD_PAGE_PROG, address, buf, bytes);
        for (i = 0; i < 5; i++)
        {
            stopMilliseconds(true,1);
            uint8_t status = mx25r_Status();
            if ((status & MX25R_FLAGS_SR_WIP) == 0)
                break;
        }

        if (i == 5) {
            return false;
        }

        address += bytes;
        buf += bytes;
        num -= bytes;
        *cnt += bytes;

        if (num)
            stopMilliseconds(true,5);
    }
    return true;
}

bool ExFlashSectorErase(uint32_t address)
{
    uint8_t status;
    int i;

    status = mx25r_Status();
    if (status & (MX25R_FLAGS_SR_WIP))
        return false;
    spi_cmd(MX25R_CMD_WRITE_ENABLE);
    spi_cmd_addr(MX25R_CMD_SECTOR_ERASE, address);
    for (i = 0; i < 5; i++)
    {
        stopMilliseconds(true,60);
        status = mx25r_Status();
        if (!(status & MX25R_FLAGS_SR_WIP))
            break;
    }
    if (i == 5)
    {
        return false;
    }
    uint8_t scr = mx25r_ReadSCR();
    if (scr & MX25R_FLAGS_SECR_E_FAIL)
        return false;

    return true;
}

void ExFlashRead(uint32_t address, uint8_t *buf, int num)
{
    spi_cmd_addr_rcv(MX25R_CMD_READ, address, buf, num);
}
