#ifndef _MMC5633_H
#define _MMC5633_H

#define MMC5633_ADDR (0x30)
#define MMC5633_PID_VAL (0x10)

enum MMC5633_Reg
{
    MMC5633_XOUT0 = 0x00,
    MMC5633_XOUT1 = 0x01,
    MMC5633_YOUT0 = 0x02,
    MMC5633_YOUT1 = 0x03,
    MMC5633_ZOUT0 = 0x04,
    MMC5633_ZOUT1 = 0x05,
    MMC5633_XOUT2 = 0x06,
    MMC5633_YOUT2 = 0x07,
    MMC5633_ZOUT2 = 0x08,
    MMC5633_TOUT  = 0x09,
    MMC5633_TPH0  = 0x0A,
    MMC5633_TPH1  = 0x0B,
    MMC5633_TU    = 0x0C,
    //
    MMC5633_STATUS0 = 0x18,
    MMC5633_STATUS1 = 0x19,
    MMC5633_ODR     = 0x1A,
    MMC5633_CTRL0   = 0x1B,
    MMC5633_CTRL1   = 0x1C,
    MMC5633_CTRL2   = 0x1D,
    MMC5633_ST_X_TH = 0x1E,
    MMC5633_ST_Y_TH = 0x1F,
    MMC5633_ST_Z_TH = 0x20,
    MMC5633_ST_X    = 0x27,
    MMC5633_ST_Y    = 0x28,
    MMC5633_ST_Z    = 0x29,
    MMC5633_PID     = 0x39
};

#define MMC5633_STATUS1_T_DONE     (1<<7)
#define MMC5633_STATUS1_M_DONE     (1<<6)
#define MMC5633_STATUS1_SAT_SENSOR (1<<5)
#define MMC5633_STATUS1_OTP_DONE   (1<<4)
#define MMC5633_STATUS1_ST_FAIL    (1<<3)
#define MMC5633_MDT_FLAG           (1<<2)

#define MMC5633_ACTIVE             (1<<7)


#define MMC5633_CTRL0_CMM_FREQ_EN  (1<<7)
#define MMC5633_CTRL0_AUTO_ST_EN   (1<<6)
#define MMC5633_CTRL0_AUTO_SR_EN   (1<<5)
#define MMC5633_CTRL0_DO_RESET     (1<<4)
#define MMC5633_CTRL0_DO_SET       (1<<3)
#define MMC5633_CTRL0_TAKE_T_MEAS  (1<<1)
#define MMC5633_CTRL0_TAKE_M_MEAS   (1)

#define MMC5633_CTRL1_SW_RESET      (1<<7)
#define MMC5633_CTRL1_ST_ENM        (1<<6)
#define MMC5633_CTRL1_ST_ENP        (1<<5)
#define MMC5633_CTRL1_ST_BW0        (0)
#define MMC5633_CTRL1_ST_BW1        (1)
#define MMC5633_CTRL1_ST_BW2        (2)
#define MMC5633_CTRL1_ST_BW3        (3)

#define MMC5633_PRD_SET(x)          ((x)&7)
#define MMC5633_EN_PRD_SET          (1<<3)
#define MMC5633_EN_CONT_MODE        (1<<4)

int mmc5633_set(void);
int mmc5633_reset(void);
int mmc5633_sample(int16_t xyz[3],int samples);
int mmc5633_init(void);
int mmc5633_get_pid(uint8_t *pid);
bool mmc5633_test(void);
#endif