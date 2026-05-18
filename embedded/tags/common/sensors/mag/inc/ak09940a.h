#ifndef __AK09940A_H__
#define __AK09940A_H__

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

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
#define AK09940A_CNTL3_200HZ (0x0A)
#define AK09940A_CNTL3_400HZ (0x0C)
#define AK09940A_CNTL3_EXTERNAL_TRIGGER (0x18)

#define AK09940A_CNTL3_SELF_TEST_MODE (0x20)

#define AK09940A_CNTL3_FIFO_EN (0x1<<7)

#define AK09940A_CNTL3_LP1 (0x0 << 5)
#define AK09940A_CNTL3_LP2 (0x1 << 5)
#define AK09940A_CNTL3_LN1 (0x2 << 5)
#define AK09940A_CNTL3_LN2 (0x3 << 5)

#define AK09940A_CNTL4_SRST (1)
#define AK09940A_I2CDIS_DISABLE (0x1B)

#define AK09940A_SENSITIVITY_NT_PER_LSB 10
#define AK09940A_SENSITIVITY_UT_PER_LSB 0.01f

typedef enum { MAG_SAMPLE_SINGLE_MODE = AK09940A_CNTL3_SINGLE_MEASURE , 
               MAG_SAMPLE_10HZ_MODE   = AK09940A_CNTL3_10HZ,
               MAG_SAMPLE_20HZ_MODE   = AK09940A_CNTL3_20HZ,
               MAG_SAMPLE_50HZ_MODE   = AK09940A_CNTL3_50HZ,
               MAG_SAMPLE_100HZ_MODE  = AK09940A_CNTL3_100HZ 
               } ak09940_mode_t;

typedef void (*TagMagPower)(void);
typedef void (*TagMagSleep)(int ms);
typedef void (*TagMagTriggerMode)(bool output);
typedef void (*TagMagTrigger)(void);
typedef bool (*TagMagDataReadyLine)(void);

typedef struct {
  const TagRegisterDevice *registers;
  TagMagPower on;
  TagMagPower off;
  TagMagSleep sleep_ms;
  TagMagTriggerMode set_trigger_output;
  TagMagTrigger trigger;
  TagMagDataReadyLine data_ready_line;
} TagMagDevice;

typedef enum {
  AK09940_RATE_10HZ = 0,
  AK09940_RATE_20HZ,
  AK09940_RATE_50HZ,
  AK09940_RATE_100HZ,
  AK09940_RATE_200HZ,
  AK09940_RATE_400HZ
} ak09940_rate_t;

typedef enum {
  AK09940_TEMP_DISABLED = 0,
  AK09940_TEMP_ENABLED = 1
} ak09940_temp_mode_t;

typedef enum {
  AK09940_DRIVE_LOW_POWER_1 = 0,
  AK09940_DRIVE_LOW_POWER_2,
  AK09940_DRIVE_LOW_NOISE_1,
  AK09940_DRIVE_LOW_NOISE_2,
  AK09940_DRIVE_ULTRA_LOW_POWER
} ak09940_drive_t;

bool magTest(void);
     // sample elements are 3 bytes so 9 bytes total
bool  magSample(bool single, uint8_t *xyz);
void  magInit(ak09940_mode_t mode);

bool ak09940aTest(const TagMagDevice *device);
bool ak09940aSample(const TagMagDevice *device, bool single, uint8_t *xyz);
void ak09940aInit(const TagMagDevice *device, ak09940_mode_t mode);

bool ak09940aCheckWhoami(const TagMagDevice *device);
msg_t ak09940aInitPowerDown(const TagMagDevice *device);
msg_t ak09940aInitContinuous(const TagMagDevice *device,
                             ak09940_rate_t rate,
                             ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode);
msg_t ak09940aInitTriggered(const TagMagDevice *device,
                            ak09940_drive_t drive,
                            ak09940_temp_mode_t temp_mode);
msg_t ak09940aTrigger(const TagMagDevice *device);
bool ak09940aDataReady(const TagMagDevice *device, bool is_continuous);
bool ak09940aReadSample(const TagMagDevice *device, int32_t *mx_raw,
                        int32_t *my_raw, int32_t *mz_raw, int16_t *temp_raw);
bool ak09940aSelfTest(const TagMagDevice *device);

bool ak09940_check_whoami(void);
msg_t ak09940_init_power_down(void);
msg_t ak09940_init_continuous(ak09940_rate_t rate, ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode);
msg_t ak09940_init_triggered(ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode);
msg_t ak09940_trigger(void);
bool ak09940_data_ready(bool is_continuous);
bool ak09940_read_sample(int32_t *mx_raw, int32_t *my_raw, int32_t *mz_raw,
                         int16_t *temp_raw);
bool ak09940_self_test(void);
void ak09940_convert_to_uT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           float *mx_uT, float *my_uT, float *mz_uT);
void ak09940_convert_to_nT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           int32_t *mx_nT, int32_t *my_nT, int32_t *mz_nT);

void magOn(void);
void magOff(void);



// https://github.com/SlimeVR/SlimeVR-Tracker-nRF/blob/master/src/sensor/mag/AK09940.c


#endif
