#ifndef __AK09940A_H__
#define __AK09940A_H__

#include <stdbool.h>

// Registers

enum AK09940A_Reg
{

AK09940A_WIA1 = (0x00),
AK09940A_WIA2 = (0x01),

// Status for polling

AK09940A_ST   = (0x0f),

// Status

AK09940A_ST1  = (0x10),

// Magnetometer data 

AK09940A_HXL  = (0x11),
AK09940A_HXM  = (0x12),
AK09940A_HXH  = (0x13),
AK09940A_HYL  = (0x14),
AK09940A_HYM  = (0x15),
AK09940A_HYH  = (0x16),
AK09940A_HZL  = (0x17),
AK09940A_HZM  = (0x18),
AK09940A_HZH  = (0x19),

// Temperature data

AK09940A_TMPS = (0x1A),

// Status for release of data
//    must be read after data

AK09940A_ST2  = (0x1B),

// Self test data

AK09940A_SXL = (0x20),
AK09940A_SXH = (0x21),
AK09940A_SYL = (0x22),
AK09940A_SYH = (0x23),
AK09940A_SZL = (0x24),
AK09940A_SZH = (0x25),

// Control Registers

AK09940A_CNTL1 = (0x30),
AK09940A_CNTL2 = (0x31),
AK09940A_CNTL3 = (0x32),
AK09940A_CNTL4 = (0x33),

// Register to release I2C

AK09940A_I2CDIS =  (0x36),

};

// Chip ID constants

#define AK09940A_COMPANY_ID (0x48)
#define AK09940A_PRODUCT_ID (0xA3)

#define AK09940A_ST_DOR_MSK  (0x2)
#define AK09940A_ST_DRDY_MSK (0x1)
#define AK09940A_ST1_FIFO_CNT_MSK (0x0f)
#define AK09940A_ST2_INV_MSK (0x02)
#define AK09940A_ST2_DOR_MSK (0x01)

#define AK09940A_CNTL1_WM_MSK (0x07)
#define AK09940A_CNTL1_MT2_MSK (0x80)
#define AK09940A_CNTL1_DTSET_MSK (0x20)

#define AK09940A_CNTL2_TEM_MSK (0x40)

#define AK09940A_CNTL3_PWRDOWN  (0x00)
#define AK09940A_CNTL3_SINGLE_MEASURE (0x01)
#define AK09940A_CNTL3_10HZ (0x02)
#define AK09940A_CNTL3_20HZ (0x04)
#define AK09940A_CNTL3_50HZ (0x06)
#define AK09940A_CNTL3_100HZ (0x08)

#define AK09940A_CNTL3_SELF_TEST_MODE (0x20)

#define AK09940A_CNTL3_FIFO_EN (0x1<<7)

#define AK09940A_CNTL3_LP1 (0x0 << 5)
#define AK09940A_CNTL3_LP2 (0x1 << 5)
#define AK09940A_CNTL3_LN1 (0x2 << 5)
#define AK09940A_CNTL3_LN2 (0x3 << 5)

#define AK09940A_CNTL4_SRST (1)
#define AK09940A_I2CDIS_DISABLE (0x1B)

typedef enum { MAG_SAMPLE_SINGLE_MODE = AK09940A_CNTL3_SINGLE_MEASURE , 
               MAG_SAMPLE_10HZ_MODE   = AK09940A_CNTL3_10HZ,
               MAG_SAMPLE_20HZ_MODE   = AK09940A_CNTL3_20HZ,
               MAG_SAMPLE_50HZ_MODE   = AK09940A_CNTL3_50HZ,
               MAG_SAMPLE_100HZ_MODE  = AK09940A_CNTL3_100HZ 
               } ak09940_mode_t;

bool magTest(void);
     // sample elements are 3 bytes so 9 bytes total
bool  magSample(bool single, uint8_t *xyz);
void  magInit(ak09940_mode_t mode);

void magOn(void);
void magOff(void);



// https://github.com/SlimeVR/SlimeVR-Tracker-nRF/blob/master/src/sensor/mag/AK09940.c


#endif