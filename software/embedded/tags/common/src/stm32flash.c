#include "hal.h"
#include "app.h"

#define FLASH_KEY1 ((uint32_t)0x45670123U) /*!< Flash key1 */
#define FLASH_KEY2 \
  ((uint32_t)0xCDEF89ABU) /*!< Flash key2: used with FLASH_KEY1 */

static inline uint32_t FLASH_Errors(void) {
  return FLASH->SR & (FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR |
                      FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                      FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                      FLASH_SR_RDERR | FLASH_SR_OPTVERR);
}

static inline void FLASH_ClearAllErrors(void) {
  SET_BIT(FLASH->SR, FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR |
                         FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                         FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                         FLASH_SR_RDERR | FLASH_SR_OPTVERR);

  SET_BIT(FLASH->ECCR, FLASH_ECCR_ECCC | FLASH_ECCR_ECCD);
}

uint32_t flash_err = 0;
void FLASH_PageErase(uint32_t Page) {
  // Check and clear all error programming flags
  // due to a previous programming. If not, PGSERR is set.

  __disable_irq();
  FLASH_ClearAllErrors();

  // set up write

  Page &= 255;  // clear any invalid bits
  MODIFY_REG(FLASH->CR, FLASH_CR_PNB, (Page << POSITION_VAL(FLASH_CR_PNB)));
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  SET_BIT(FLASH->CR, FLASH_CR_STRT);

  while (FLASH->SR & FLASH_SR_BSY)
    ;

  CLEAR_BIT(FLASH->CR, FLASH_CR_PER);
  __enable_irq();
  flash_err = FLASH_Errors();
}

void FLASH_Unlock(void) {
  // Authorize the FLASH Registers access

  WRITE_REG(FLASH->KEYR, FLASH_KEY1);
  WRITE_REG(FLASH->KEYR, FLASH_KEY2);
}

void FLASH_Lock(void) {
  // Set the LOCK Bit to lock the FLASH Registers access

  SET_BIT(FLASH->CR, FLASH_CR_LOCK);
}

// See page 82 of reference manual

void FLASH_Program_DoubleWord(uint32_t *Address, uint32_t Data0,
                              uint32_t Data1) {
  // only aligned addresses

  if (((uint32_t)Address) & 0x7) return;

  FLASH_ClearAllErrors();

  __disable_irq();

  // Set PG bit

  SET_BIT(FLASH->CR, FLASH_CR_PG);

  // Program the double word

  /*  *(__IO uint32_t*)*/ Address[0] = Data0;
  /* *(__IO uint32_t*)(*/ Address[1] = Data1;

  while (FLASH->SR & FLASH_SR_BSY)
    ;

  // Check that EOP flag is set in the FLASH_SR register
  // (meaning that the programming operation has succeed), and clear it by
  // software.

  if (FLASH->SR & FLASH_SR_EOP) CLEAR_BIT(FLASH->SR, FLASH_SR_EOP);

  // Reset PG bit

  CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
  __enable_irq();
  flash_err = FLASH_Errors();
}

void FLASH_Flush_Data_Cache(void) {
  if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN) != RESET) {
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);  // disable data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCRST);   // reset data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCEN);    // enable data cache
  }
}

uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words) {
    for (int i = 0; i < words; i += 2) {
      //if (i) stopMilliseconds(false,4);
      FLASH_Program_DoubleWord(&Address[i], array[i], array[i+1]);
      if (flash_err) return flash_err;
    }
    return 0;
}

