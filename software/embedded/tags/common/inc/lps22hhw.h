#ifndef LPS22HW_H
#define LPS22HW_H

enum LPS22_OutputRate
{
    LPS22_OutputRate_OneShot = 0b000,
    LPS22_OutputRate_1Hz = 0b001,
    LPS22_OutputRate_10Hz = 0b010,
    LPS22_OutputRate_25Hz = 0b011,
    LPS22_OutputRate_50Hz = 0b100,
    LPS22_OutputRate_75Hz = 0b101,
    LPS22_OutputRate_100Hz = 0b110,
    LPS22_OutputRate_200Hz = 0b111
};

enum LPS22_LowPassFilter
{
    LPS22_LowPassFilter_Off = 0b00,
    LPS22_LowPassFilter_ODR9 = 0b10,
    LPS22_LowPassFilter_ODR20 = 0b011,
};

enum LPS22_Reg
{
    LPS22_INTERRUPT_CFG = 0x0B,
    LPS22_THS_P_L = 0x0C,
    LPS22_THS_P_H = 0x0D,
    LPS22_IF_CTRL = 0x0E,
    LPS22_WHO_AM_I = 0x0F,
    LPS22_CTRL_REG1 = 0x10,
    LPS22_CTRL_REG2 = 0x11,
    LPS22_CTRL_REG3 = 0x12,
    LPS22_FIFO_CTRL = 0x13,
    LPS22_FIFO_WTM  = 0x14,
    LPS22_REF_P_L = 0x15,
    LPS22_REF_P_H = 0x16,
    LPS22_RPDS_L = 0x18,
    LPS22_RPDS_H = 0x19,
    LPS22_INT_SOURCE = 0x24,
    LPS22_FIFO_STATUS1 = 0x25,
    LPS22_FIFO_STATUS2 = 0x26,
    LPS22_STATUS = 0x27,
    LPS22_PRESS_OUT_XL = 0x28,
    LPS22_PRESS_OUT_L = 0x29,
    LPS22_PRESS_OUT_H = 0x2A,
    LPS22_TEMP_OUT_L = 0x2B,
    LPS22_TEMP_OUT_H = 0x2C,
    LPS22_LPFP_RES = 0x33,
    LPS22_FIFO_DATA_OUT_PRES_XL = 0x78,
    LPS22_FIFO_DATA_OUT_PRES_L = 0x79,
    LPS22_FIFO_DATA_OUT_PRES_H = 0x7A,
    LPS22_FIFO_DATA_OUT_TEMP_L = 0x7B,
    LPS22_FIFO_DATA_OUT_TEMP_H = 0x7C,
};

#define LPS22_WHO_AM_I_VALUE (0xB3)

#define LPS22_CTRL_REG1_SIM        (1<<0)
#define LPS22_CTRL_REG1_BDU        (1<<1)

#define LPS22_CTRL_REG2_ONE_SHOT   (1<<0)
#define LPS22_CTRL_REG2_LOW_NOISE_EN (1<<1)
#define LPS22_CTRL_REG2_IF_ADD_INC (1<<4)

#define LPS22_CTRL_REG3_DRDY       (1<<2)

#define LPS22_RES_CONF_LC_EN       (1<<0)

#endif