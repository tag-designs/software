#ifndef _MX25R_H_
#define _MX25R_H_

#define MX25R_MANUFACTURE_ID                  ((uint8_t) 0xC2)
#define MX25R_MEMORY_TYPE_ID                  ((uint8_t) 0x28)
#define MX25R_MEMORY_TYPE                     (DID[0])
#define MX25R_SIZE (DID)                      (1<<(DID[1])
#define MX25R_SECTOR_SIZE                     (4096)
#define MX25R_SIZE                            (0x17)

/* Status Register */

#define MX25R_FLAGS_SR_WIP                    ((uint8_t)0x01)    /* Write in progress */
#define MX25R_FLAGS_SR_WEL                    ((uint8_t)0x02)    /* Write enable latch */
#define MX25R_FLAGS_SR_BP                     ((uint8_t)0x3C)    /* Block protect */
#define MX25R_FLAGS_SR_QE                     ((uint8_t)0x40)    /* Quad enable */
#define MX25R_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /* Status register write disable */

/* Configuration Register 1 */

#define MX25R_FLAGS_CR1_TB                    ((uint8_t)0x08)    /* Top / bottom */

/* Configuration Register 2 */

#define MX25R_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    /* Low power / high performance switch */

/* Security Register */

#define MX25R_FLAGS_SECR_SOI                  ((uint8_t)0x01)    /* Secured OTP indicator */
#define MX25R_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    /* Lock-down secured OTP */
#define MX25R_FLAGS_SECR_PSB                  ((uint8_t)0x04)    /* Program suspend bit */
#define MX25R_FLAGS_SECR_ESB                  ((uint8_t)0x08)    /* Erase suspend bit */
#define MX25R_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    /* Program fail flag */
#define MX25R_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    /* Erase fail flag */

#endif 