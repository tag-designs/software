#ifndef LPS33HW_H
#define LPS33HW_H

enum LPS33_OutputRate
{
    LPS33_OutputRate_OneShot = 0b000,
    LPS33_OutputRate_1Hz = 0b001,
    LPS33_OutputRate_10Hz = 0b010,
    LPS33_OutputRate_25Hz = 0b011,
    LPS33_OutputRate_50Hz = 0b100,
    LPS33_OutputRate_75Hz = 0b101
};

enum LPS33_LowPassFilter
{
    LPS33_LowPassFilter_Off = 0b00,
    LPS33_LowPassFilter_ODR9 = 0b10,
    LPS33_LowPassFilter_ODR20 = 0b011,
};

enum LPS33_Reg
{
    LPS33_INTERRUPT_CFG = 0x0B,
    LPS33_THS_P_L = 0x0C,
    LPS33_THS_P_H = 0x0D,
    LPS33_WHO_AM_I = 0x0F,
    LPS33_CTRL_REG1 = 0x10,
    LPS33_CTRL_REG2 = 0x11,
    LPS33_CTRL_REG3 = 0x12,
    LPS33_FIFO_CTRL = 0x14,
    LPS33_REF_P_XL = 0x15,
    LPS33_REF_P_L = 0x16,
    LPS33_REF_P_H = 0x17,
    LPS33_RPDS_L = 0x18,
    LPS33_RPDS_H = 0x19,
    LPS33_RES_CONF = 0x1A,
    LPS33_INT_SOURCE = 0x25,
    LPS33_FIFO_STATUS = 0x26,
    LPS33_STATUS = 0x27,
    LPS33_PRESS_OUT_XL = 0x28,
    LPS33_PRESS_OUT_L = 0x29,
    LPS33_PRESS_OUT_H = 0x2A,
    LPS33_TEMP_OUT_L = 0x2B,
    LPS33_TEMP_OUT_H = 0x2C,
    LPS33_LPFP_RES = 0x33
};

#define LPS33_WHO_AM_I_VALUE (0xB1)

#define LPS33_CTRL_REG2_ONE_SHOT   (1<<0)
#define LPS33_CTRL_REG2_IF_ADD_INC (1<<4)

#define LPS33_CTRL_REG3_DRDY       (1<<2)

#define LPS33_RES_CONF_LC_EN       (1<<0)

extern void lps33_On(void);
extern void lps33_Off(void);

extern void lps33_GetReg(enum LPS33_Reg reg, uint8_t *val, int num);
extern void lps33_SetReg(enum LPS33_Reg reg, unsigned char *val, int num);

extern bool GetPressureTemp(int16_t *pressure, int16_t *temperature);
#endif