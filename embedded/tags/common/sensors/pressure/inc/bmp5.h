
#ifndef BMP5_H_
#define BMP5_H_

#include <stdint.h>

enum BMP5_Reg
{
    BMP5_REG_CHIP_ID =         0x01,        
    BMP5_REG_REV_ID  =         0x02,        
    BMP5_REG_CHIP_STATUS  =    0x11,        
    BMP5_REG_DRIVE_CONFIG =    0x13,        
    BMP5_REG_INT_CONFIG =      0x14,        
    BMP5_REG_INT_SOURCE =      0x15,        
    BMP5_REG_FIFO_CONFIG =     0x16,        
    BMP5_REG_FIFO_COUNT =      0x17,        
    BMP5_REG_FIFO_SEL =        0x18,        
    BMP5_REG_TEMP_DATA_XLSB =  0x1D,      
    BMP5_REG_TEMP_DATA_LSB  =  0x1E,      
    BMP5_REG_TEMP_DATA_MSB  =  0x1F,      
    BMP5_REG_PRESS_DATA_XLSB = 0x20,     
    BMP5_REG_PRESS_DATA_LSB =  0x21,     
    BMP5_REG_PRESS_DATA_MSB =  0x22,     
    BMP5_REG_INT_STATUS =      0x27,     
    BMP5_REG_STATUS =          0x28,     
    BMP5_REG_FIFO_DATA =       0x29,     
    BMP5_REG_NVM_ADDR =        0x2B,     
    BMP5_REG_NVM_DATA_LSB =    0x2C,     
    BMP5_REG_DSP_CONFIG =      0x30,     
    BMP5_REG_DSP_IIR  =        0x31,     
    BMP5_REG_OOR_THR_P_LSB =   0x32,     
    BMP5_REG_OOR_THR_P_MSB =   0x33,     
    BMP5_REG_OOR_RANGE =       0x34,     
    BMP5_REG_OOR_CONFIG =      0x35,     
    BMP5_REG_OSR_CONFIG =      0x36,     
    BMP5_REG_ODR_CONFIG =      0x37,     
    BMP5_REG_OSR_EFF =         0x38,     
    BMP5_REG_CMD =             0x7E
};                   


#define BMP5_CHIP_ID_VALUE (0x50)

/*  I2C addresses */
#define BMP5_I2C_ADDR_PRIM                        (uint8_t)(0x46)
#define BMP5_I2C_ADDR_SEC                         (uint8_t)(0x47)

/*  NVM addresses */

#define BMP5_NVM_START_ADDR                       (uint8_t)(0x20)
#define BMP5_NVM_END_ADDR                         (uint8_t)(0x22)

/*  Interface settings */

#define BMP5_SPI_RD_MASK                          (uint8_t)(0x80)

/*  Delay definition */

#define BMP5_DELAY_MS_POWER_ON                    5
#define BMP5_DELAY_MS_SOFT_RESET                  2
#define BMP5_DELAY_MS_STANDBY                     3
#define BMP5_DELAY_MS_NVM_READY_READ              2
#define BMP5_DELAY_MS_NVM_READY_WRITE             10

/*  Soft reset command */

#define BMP5_SOFT_RESET_CMD                       0xB6

/* NVM command */

#define BMP5_NVM_FIRST_CMND                       (uint8_t)(0x5D)
#define BMP5_NVM_READ_ENABLE_CMND                 (uint8_t)(0xA5)
#define BMP5_NVM_WRITE_ENABLE_CMND                (uint8_t)(0xA0)

/*  Deepstandby enable/disable */

#define BMP5_DEEP_ENABLED                         (uint8_t)(0)
#define BMP5_DEEP_DISABLED                        (uint8_t)(1)

/*  ODR settings */

#define BMP5_ODR_240_HZ                           (uint8_t)(0x00)
#define BMP5_ODR_218_5_HZ                         (uint8_t)(0x01)
#define BMP5_ODR_199_1_HZ                         (uint8_t)(0x02)
#define BMP5_ODR_179_2_HZ                         (uint8_t)(0x03)
#define BMP5_ODR_160_HZ                           (uint8_t)(0x04)
#define BMP5_ODR_149_3_HZ                         (uint8_t)(0x05)
#define BMP5_ODR_140_HZ                           (uint8_t)(0x06)
#define BMP5_ODR_129_8_HZ                         (uint8_t)(0x07)
#define BMP5_ODR_120_HZ                           (uint8_t)(0x08)
#define BMP5_ODR_110_1_HZ                         (uint8_t)(0x09)
#define BMP5_ODR_100_2_HZ                         (uint8_t)(0x0A)
#define BMP5_ODR_89_6_HZ                          (uint8_t)(0x0B)
#define BMP5_ODR_80_HZ                            (uint8_t)(0x0C)
#define BMP5_ODR_70_HZ                            (uint8_t)(0x0D)
#define BMP5_ODR_60_HZ                            (uint8_t)(0x0E)
#define BMP5_ODR_50_HZ                            (uint8_t)(0x0F)
#define BMP5_ODR_45_HZ                            (uint8_t)(0x10)
#define BMP5_ODR_40_HZ                            (uint8_t)(0x11)
#define BMP5_ODR_35_HZ                            (uint8_t)(0x12)
#define BMP5_ODR_30_HZ                            (uint8_t)(0x13)
#define BMP5_ODR_25_HZ                            (uint8_t)(0x14)
#define BMP5_ODR_20_HZ                            (uint8_t)(0x15)
#define BMP5_ODR_15_HZ                            (uint8_t)(0x16)
#define BMP5_ODR_10_HZ                            (uint8_t)(0x17)
#define BMP5_ODR_05_HZ                            (uint8_t)(0x18)
#define BMP5_ODR_04_HZ                            (uint8_t)(0x19)
#define BMP5_ODR_03_HZ                            (uint8_t)(0x1A)
#define BMP5_ODR_02_HZ                            (uint8_t)(0x1B)
#define BMP5_ODR_01_HZ                            (uint8_t)(0x1C)
#define BMP5_ODR_0_5_HZ                           (uint8_t)(0x1D)
#define BMP5_ODR_0_250_HZ                         (uint8_t)(0x1E)
#define BMP5_ODR_0_125_HZ                         (uint8_t)(0x1F)

/*  Oversampling for temperature and pressure */

#define BMP5_OVERSAMPLING_1X                      (uint8_t)(0x00)
#define BMP5_OVERSAMPLING_2X                      (uint8_t)(0x01)
#define BMP5_OVERSAMPLING_4X                      (uint8_t)(0x02)
#define BMP5_OVERSAMPLING_8X                      (uint8_t)(0x03)
#define BMP5_OVERSAMPLING_16X                     (uint8_t)(0x04)
#define BMP5_OVERSAMPLING_32X                     (uint8_t)(0x05)
#define BMP5_OVERSAMPLING_64X                     (uint8_t)(0x06)
#define BMP5_OVERSAMPLING_128X                    (uint8_t)(0x07)

/*  IIR filter for temperature and pressure */

#define BMP5_IIR_FILTER_BYPASS                    (uint8_t)(0x00)
#define BMP5_IIR_FILTER_COEFF_1                   (uint8_t)(0x01)
#define BMP5_IIR_FILTER_COEFF_3                   (uint8_t)(0x02)
#define BMP5_IIR_FILTER_COEFF_7                   (uint8_t)(0x03)
#define BMP5_IIR_FILTER_COEFF_15                  (uint8_t)(0x04)
#define BMP5_IIR_FILTER_COEFF_31                  (uint8_t)(0x05)
#define BMP5_IIR_FILTER_COEFF_63                  (uint8_t)(0x06)
#define BMP5_IIR_FILTER_COEFF_127                 (uint8_t)(0x07)

/* Fifo frame configuration */

#define BMP5_FIFO_EMPTY                           (uint8_t)(0X7F)
#define BMP5_FIFO_MAX_THRESHOLD_P_T_MODE          (uint8_t)(0x0F)
#define BMP5_FIFO_MAX_THRESHOLD_P_MODE            (uint8_t)(0x1F)

/*  Macro is used to bypass both iir_t and iir_p together */
#define BMP5_IIR_BYPASS                           (uint8_t)(0xC0)

/*  Pressure Out-of-range count limit */
#define BMP5_OOR_COUNT_LIMIT_1                    (uint8_t)(0x00)
#define BMP5_OOR_COUNT_LIMIT_3                    (uint8_t)(0x01)
#define BMP5_OOR_COUNT_LIMIT_7                    (uint8_t)(0x02)
#define BMP5_OOR_COUNT_LIMIT_15                   (uint8_t)(0x03)

/*  Interrupt configurations */
#define BMP5_INT_MODE_PULSED                      (uint8_t)(0)
#define BMP5_INT_MODE_LATCHED                     (uint8_t)(1)

#define BMP5_INT_POL_ACTIVE_LOW                   (uint8_t)(0)
#define BMP5_INT_POL_ACTIVE_HIGH                  (uint8_t)(1)

#define BMP5_INT_OD_PUSHPULL                      (uint8_t)(0)
#define BMP5_INT_OD_OPENDRAIN                     (uint8_t)(1)

/*  NVM and Interrupt status asserted macros */

#define BMP5_INT_ASSERTED_DRDY                    (uint8_t)(0x01)
#define BMP5_INT_ASSERTED_FIFO_FULL               (uint8_t)(0x02)
#define BMP5_INT_ASSERTED_FIFO_THRES              (uint8_t)(0x04)
#define BMP5_INT_ASSERTED_PRESSURE_OOR            (uint8_t)(0x08)
#define BMP5_INT_ASSERTED_POR_SOFTRESET_COMPLETE  (uint8_t)(0x10)
#define BMP5_INT_NVM_RDY                          (uint8_t)(0x02)
#define BMP5_INT_NVM_ERR                          (uint8_t)(0x04)
#define BMP5_INT_NVM_CMD_ERR                      (uint8_t)(0x08)

/*  Interrupt configurations */
#define BMP5_INT_MODE_MSK                         (uint8_t)(0x01)

#define BMP5_INT_POL_MSK                          (uint8_t)(0x02)
#define BMP5_INT_POL_POS                          (uint8_t)(1)

#define BMP5_INT_OD_MSK                           (uint8_t)(0x04)
#define BMP5_INT_OD_POS                           (uint8_t)(2)

#define BMP5_INT_EN_MSK                           (uint8_t)(0x08)
#define BMP5_INT_EN_POS                           (uint8_t)(3)

#define BMP5_INT_DRDY_EN_MSK                      (uint8_t)(0x01)

#define BMP5_INT_FIFO_FULL_EN_MSK                 (uint8_t)(0x02)
#define BMP5_INT_FIFO_FULL_EN_POS                 (uint8_t)(1)

#define BMP5_INT_FIFO_THRES_EN_MSK                (uint8_t)(0x04)
#define BMP5_INT_FIFO_THRES_EN_POS                (uint8_t)(2)

#define BMP5_INT_OOR_PRESS_EN_MSK                 (uint8_t)(0x08)
#define BMP5_INT_OOR_PRESS_EN_POS                 (uint8_t)(3)

/*  ODR configuration */

#define BMP5_ODR_MSK                              (uint8_t)(0x7C)
#define BMP5_ODR_POS                              (uint8_t)(2)

/*  OSR configurations */
#define BMP5_TEMP_OS_MSK                          (uint8_t)(0x07)

#define BMP5_PRESS_OS_MSK                         (uint8_t)(0x38)
#define BMP5_PRESS_OS_POS                         (uint8_t)(3)

/*  Pressure enable */
#define BMP5_PRESS_EN_MSK                         (uint8_t)(0x40)
#define BMP5_PRESS_EN_POS                         (uint8_t)(6)

/*  IIR configurations */
#define BMP5_SET_IIR_TEMP_MSK                     (uint8_t)(0x07)

#define BMP5_SET_IIR_PRESS_MSK                    (uint8_t)(0x38)
#define BMP5_SET_IIR_PRESS_POS                    (uint8_t)(3)

#define BMP5_OOR_SEL_IIR_PRESS_MSK                (uint8_t)(0x80)
#define BMP5_OOR_SEL_IIR_PRESS_POS                (uint8_t)(7)

#define BMP5_SHDW_SET_IIR_TEMP_MSK                (uint8_t)(0x08)
#define BMP5_SHDW_SET_IIR_TEMP_POS                (uint8_t)(3)

#define BMP5_SHDW_SET_IIR_PRESS_MSK               (uint8_t)(0x20)
#define BMP5_SHDW_SET_IIR_PRESS_POS               (uint8_t)(5)

#define BMP5_SET_FIFO_IIR_TEMP_MSK                (uint8_t)(0x10)
#define BMP5_SET_FIFO_IIR_TEMP_POS                (uint8_t)(4)

#define BMP5_SET_FIFO_IIR_PRESS_MSK               (uint8_t)(0x40)
#define BMP5_SET_FIFO_IIR_PRESS_POS               (uint8_t)(6)

#define BMP5_IIR_FLUSH_FORCED_EN_MSK              (uint8_t)(0x04)
#define BMP5_IIR_FLUSH_FORCED_EN_POS              (uint8_t)(2)

/*  Effective OSR configurations and ODR valid status */

#define BMP5_OSR_TEMP_EFF_MSK                     (uint8_t)(0x07)

#define BMP5_OSR_PRESS_EFF_MSK                    (uint8_t)(0x38)
#define BMP5_OSR_PRESS_EFF_POS                    (uint8_t)(3)

#define BMP5_ODR_IS_VALID_MSK                     (uint8_t)(0x80)
#define BMP5_ODR_IS_VALID_POS                     (uint8_t)(7)

/*  Powermode */
#define BMP5_POWERMODE_MSK                        (uint8_t)(0x03)

#define BMP5_DEEP_DISABLE_MSK                     (uint8_t)(0x80)
#define BMP5_DEEP_DISABLE_POS                     (uint8_t)(7)

/*  Fifo configurations */

#define BMP5_FIFO_THRESHOLD_MSK                   (uint8_t)(0x1F)

#define BMP5_FIFO_MODE_MSK                        (uint8_t)(0x20)
#define BMP5_FIFO_MODE_POS                        (uint8_t)(5)

#define BMP5_FIFO_DEC_SEL_MSK                     (uint8_t)(0x1C)
#define BMP5_FIFO_DEC_SEL_POS                     (uint8_t)(2)

#define BMP5_FIFO_COUNT_MSK                       (uint8_t)(0x3F)

#define BMP5_FIFO_FRAME_SEL_MSK                   (uint8_t)(0x03)

/*  Out-of-range configuration */

#define BMP5_OOR_THR_P_LSB_MSK                    (uint32_t)(0x0000FF)

#define BMP5_OOR_THR_P_MSB_MSK                    (uint32_t)(0x00FF00)

#define BMP5_OOR_THR_P_XMSB_MSK                   (uint32_t)(0x010000)
#define BMP5_OOR_THR_P_XMSB_POS                   (uint16_t)(16)

/* Macro to mask xmsb value of oor threshold from register(0x35) value */

#define BMP5_OOR_THR_P_XMSB_REG_MSK               (uint8_t)(0x01)

#define BMP5_OOR_COUNT_LIMIT_MSK                  (uint8_t)(0xC0)
#define BMP5_OOR_COUNT_LIMIT_POS                  (uint8_t)(6)

/*  NVM configuration */

#define BMP5_NVM_ADDR_MSK                         (uint8_t)(0x3F)

#endif 