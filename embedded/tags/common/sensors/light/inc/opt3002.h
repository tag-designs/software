#ifndef __OPT3002_H_
#define __OPT3002_H_

#define OPT3002_ADR 0x44

#define OPT3002_CFG_RN_MASK  (0xf000)
#define OPT3002_CFG_CT_MASK  (0x0800)
#define OPT3002_CFG_M_MASK   (0x0600)
#define OPT3002_CFG_OVF_MASK (0x0100)
#define OPT3002_CFG_CRF_MASK (0x0080)
#define OPT3002_CFG_F_MASK   (0x0060)
#define OPT3002_CFG_L_MASK   (0x0010)
#define OPT3002_CFG_POL_MASK (0x0008)
#define OPT3002_CFG_ME_MASK  (0x0004)
#define OPT3002_CFG_FC_MASK  (0x0003)

#define OPT3002_CFG_AUTO_RANGE   (0xC000)
#define OPT3002_CFG_100ms        (0)
#define OPT3002_CFG_800ms        (1<<11)
#define OPT3002_CFG_Shutdown     (0)
#define OPT3002_CFG_SingleShot   (1 << 9)

#define OPT3002_CFG_HIGH (OPT3002_CFG_AUTO_RANGE|OPT3002_CFG_800ms|OPT3002_CFG_SingleShot)
#define OPT3002_CFG_DEFAULT (OPT3002_CFG_AUTO_RANGE|OPT3002_CFG_SingleShot)
enum OPT3002_REG { OPT3002_Result        = 0x00,
                   OPT3002_Configuration = 0x01,
                   OPT3002_LowLimit      = 0x02,
                   OPT3002_HighLimit     = 0x03,
                   OPT3002_ManID         = 0x7E
};

#define OPT3002_MAN_ID 0x5449

float opt3002_lux(uint16_t sample);
bool opt3002_get_sample(uint16_t *sample);
uint16_t opt3002_get_id(void);
bool opt3002_test(void);

#endif