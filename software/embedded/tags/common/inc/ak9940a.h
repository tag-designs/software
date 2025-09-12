#ifndef __AK9940A_H__
#define __AK9940A_H__

// Registers

#define AK9940A_WIA1 (0x00)
#define AK9940A_WIA2 (0x01)
#define AK9940A_ST   (0x0f)

#define AK9940A_ST1  (0x10)
#define AK9940A_HXL  (0x11)
#define AK9940A_HXM  (0x12)
#define AK9940A_HXH  (0x13)
#define AK9940A_HYL  (0x14)
#define AK9940A_HYM  (0x15)
#define AK9940A_HYH  (0x16)
#define AK9940A_HZL  (0x17)
#define AK9940A_HZM  (0x18)
#define AK9940A_HZH  (0x19)
#define AK9940A_TMPS (0x1A)
#define AK9940A_ST2  (0x1B)

#define AK9940A_SXL  (0x20)
#define AK9940A_SXH  (0x21)
#define AK9940A_SYL  (0x22)
#define AK9940A_SYH  (0x23)
#define AK9940A_SZL  (0x24)
#define AK9940A_SZH  (0x25)

#define AK9940A_CNTL1 (0x30)
#define AK9940A_CNTL2 (0x31)
#define AK9940A_CNTL3 (0x32)
#define AK9940A_CNTL4 (0x33)

#define AK9940A_I2CDIS (0x36)

#define AK9940A_COMPANY_ID (0x48)
#define AX9940A_PRODUCT_ID (0xA3)

#define AK9940A_ST_DOR_MSK  (0x2)
#define AK9940A_ST_DRDY_MSK (0x1)
#define AK9940A_ST1_FIFO_CNT_MSK (0x0f)
#define AK9940A_ST2_INV_MSK (0x02)
#define AK9940A_ST2_DOR_MSK (0x01)

#define AK9940A_CNTL1_WM_MSK (0x07)
#define AK9940A_CNTL1_MT2_MSK (0x80)
#define AK9940A_CNTL1_DTSET_MSK (0x20)

#define AK9940A_CNTL2_TEM_MSK (0x40)

#define AK9940A_CNTL3_PWRDOWN  (0x00)
#define AK9940A_CNTL3_SINGLE_MEASURE (0x01)
#define AK9940A_CNTL3_SELF_TEST_MODE (0x20)

#define AK9940A_CNTL3_FIFO_EN (0x1<<7)

#define AK9940A_CNTL3_LP1 (0x0 << 5)
#define AK9940A_CNTL3_LP2 (0x1 << 5)
#define AK9940A_CNTL3_LN1 (0x2 << 5)
#define AK9940A_CNTL3_LN2 (0x3 << 5)

#define AK9940A_CNTL4_SRST (1)
#define AK9940A_I2CDIS_DISABLE (0x1B)




#endif