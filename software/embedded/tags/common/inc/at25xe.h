#ifndef _AT25XE_H_
#define _AT25XE_H_

#define AT25XE_MANUFACTURE_ID                  ((uint8_t) 0x1F)
#define AT25XE_MEMORY_TYPE_ID                  ((uint8_t) 0x47)
#define AT25XE_MEMORY_TYPE                     (DID[0])
//#define AT25XE_SIZE (DID)                      (1<<(DID[1])
#define AT25XE321_SIZE                          0x08
#define AT25XE_SECTOR_SIZE                     (4096)

#define AT25XE_POWER_DOWN_DELAY_US           (3)
#define AT25XE_RELEASE_POWER_DOWN_DELAY_US   (24) //16us Typical // 24uS max
#define AT25XE_POWER_UP_DELAY_US             (260)

/* Status Register */

#define AT25XE_FLAGS_SR_WIP                    ((uint8_t)0x01)    /* Write in progress */
#define AT25XE_FLAGS_SR_WEL                    ((uint8_t)0x02)    /* Write enable latch */
#define AT25XE_FLAGS_SR_BP                     ((uint8_t)0x3C)    /* Block protect */
#define AT25XE_FLAGS_SR_QE                     ((uint8_t)0x40)    /* Quad enable */
#define AT25XE_FLAGS_SR_SRWD                   ((uint8_t)0x80)    /* Status register write disable */

/* Configuration Register 1 */

#define AT25XE_FLAGS_CR1_TB                    ((uint8_t)0x08)    /* Top / bottom */

/* Configuration Register 2 */

#define AT25XE_FLAGS_CR2_LH_SWITCH             ((uint8_t)0x02)    /* Low power / high performance switch */

/* Security Register */

#define AT25XE_FLAGS_SECR_SOI                  ((uint8_t)0x01)    /* Secured OTP indicator */
#define AT25XE_FLAGS_SECR_LDSO                 ((uint8_t)0x02)    /* Lock-down secured OTP */
#define AT25XE_FLAGS_SECR_PSB                  ((uint8_t)0x04)    /* Program suspend bit */
#define AT25XE_FLAGS_SECR_ESB                  ((uint8_t)0x08)    /* Erase suspend bit */
#define AT25XE_FLAGS_SECR_P_FAIL               ((uint8_t)0x20)    /* Program fail flag */
#define AT25XE_FLAGS_SECR_E_FAIL               ((uint8_t)0x40)    /* Erase fail flag */

enum AT25XE_ERR {AT25XE_OK, AT25XE_PFAIL, AT25XE_EFAIL, AT25XE_WFAIL,AT25XE_WIP, AT25XE_nWEL};

struct at25xe_id {
    uint8_t mid;
    uint8_t memtype;
    uint8_t memsz;
};

uint8_t at25xe_Status(void);
void at25xe_ReadID(struct at25xe_id *id);
uint8_t at25xe_ReadSCR(void);
uint16_t at25xe_ReadCFG(void);
enum AT25XE_ERR at25xe_Write(uint32_t address, uint8_t *buf, int *cnt);
enum AT25XE_ERR at25xe_SectorErase(uint32_t address);
void at25xe_Read(uint32_t address, uint8_t *buf, int num);

void at25xe_reset(void);
void at25xe_deepPwrDown(void);
void at25xe_deepPwrUp(void);

// Write Status and Config registers

  
#endif 