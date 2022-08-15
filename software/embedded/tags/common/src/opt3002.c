#include "hal.h"
#include "custom.h"
#include "app.h"
#include "config.h"
#include "persistent.h"
#include "external_flash.h"
#include "opt3002.h"

#define OPT3002_TIMEOUT 500

extern void optOn(void);
extern void optOff(void);

static int I2C1_MemWrite(uint8_t device, uint8_t reg, unsigned char *buffer,
                         uint16_t size, uint32_t timeout)
{
    uint8_t txbuf[10];
    txbuf[0] = reg;
    for (int i = 0; i < 8 && i < size; i++)
    {
        txbuf[i + 1] = buffer[i];
    }
    return i2cMasterTransmitTimeout(&I2CD1, device, txbuf, size + 1, 0, 0,
                                    timeout);
}

static int opt3002_GetReg(enum OPT3002_REG reg, uint16_t *val)
{
    uint8_t buf[2];
    int err = i2cMasterTransmitTimeout(&I2CD1, OPT3002_ADR, &reg, 1, buf, 2,
                                       OPT3002_TIMEOUT);
    *val = buf[0] << 8 | buf[1];
    return err;
}

static int opt3002_SetReg(enum OPT3002_REG reg, uint16_t val)
{
    uint8_t buf[2];
    buf[0] = val >> 8;
    buf[1] = val & 0xff;
    return I2C1_MemWrite(OPT3002_ADR, reg, buf, 2, OPT3002_TIMEOUT);
}

float opt3002_lux(uint16_t sample)
{
    uint32_t exponent = (sample >> 12) & 0xf;
    return (1 << (exponent)) * 1.2 * (sample & 0xfff);
}

bool opt3002_get_sample(uint16_t *sample)
{
    int err = -1;
    //optOn();
    err = opt3002_SetReg(OPT3002_Configuration,OPT3002_CFG_DEFAULT);
    if (!err) {
        stopMilliseconds(false,125);
        err = opt3002_GetReg(OPT3002_Result,sample);
    }
    //optOff();
    if (err) {
        *sample = 0;
        return false;
    }
    return true;
}

uint16_t opt3002_get_id(void)
{
    uint16_t value;
    optOn();
    opt3002_GetReg(OPT3002_ManID, &value);
    optOff();
    return value;
}

bool opt3002_test(void) {
    return OPT3002_MAN_ID == opt3002_get_id();
}
