/**
 * @file stm32flash.c
 * @brief Low-level STM32 internal flash erase and program helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "flash_internal.h"

#define FLASH_KEY1 ((uint32_t)0x45670123U) /*!< Flash key1 */
#define FLASH_KEY2 \
  ((uint32_t)0xCDEF89ABU) /*!< Flash key2: used with FLASH_KEY1 */

/** @name Flash status helpers
 * Small helpers that keep persistent.c from needing to know STM32 status and
 * ECC flag details.
 * @{
 */
/**
 * @brief Read the STM32 flash status bits that indicate program/erase errors.
 *
 * @return Bitmask of currently asserted flash status flags.
 */
static inline uint32_t FLASH_Errors(void) {
  return FLASH->SR & (FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR |
                      FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                      FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                      FLASH_SR_RDERR | FLASH_SR_OPTVERR);
}

/**
 * @brief Clear all sticky flash status and ECC error flags before an operation.
 */
static inline void FLASH_ClearAllErrors(void) {
  SET_BIT(FLASH->SR, FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR |
                         FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                         FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                         FLASH_SR_RDERR | FLASH_SR_OPTVERR);

  SET_BIT(FLASH->ECCR, FLASH_ECCR_ECCC | FLASH_ECCR_ECCD);
}
/** @} */

uint32_t flash_err = 0;

/** @name Internal flash programming
 * Operations used by persistent storage to erase pages and append aligned flash
 * records without exposing STM32 register sequencing to higher layers.
 * @{
 */
/**
 * @brief Erase one internal flash page.
 *
 * @param[in] Page Flash page number to erase.
 */
void FLASH_PageErase(uint32_t Page) {
  __disable_irq();
  FLASH_ClearAllErrors();

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

/**
 * @brief Unlock internal flash control registers for an erase or program step.
 */
void FLASH_Unlock(void) {
  WRITE_REG(FLASH->KEYR, FLASH_KEY1);
  WRITE_REG(FLASH->KEYR, FLASH_KEY2);
}

/**
 * @brief Lock internal flash control registers against accidental writes.
 */
void FLASH_Lock(void) {
  SET_BIT(FLASH->CR, FLASH_CR_LOCK);
}

/**
 * @brief Program one aligned STM32 double-word.
 *
 * @param[in,out] Address Aligned destination address in internal flash.
 * @param[in] Data0 Lower 32 bits to program.
 * @param[in] Data1 Upper 32 bits to program.
 */
void FLASH_Program_DoubleWord(uint32_t *Address, uint32_t Data0,
                              uint32_t Data1) {
  if (((uint32_t)Address) & 0x7) return;

  FLASH_ClearAllErrors();

  __disable_irq();

  SET_BIT(FLASH->CR, FLASH_CR_PG);

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

/**
 * @brief Flush the STM32 data cache after flash contents change.
 */
void FLASH_Flush_Data_Cache(void) {
  if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN) != RESET) {
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);  // disable data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCRST);   // reset data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCEN);    // enable data cache
  }
}

/**
 * @brief Program an array of 32-bit words as aligned flash double-words.
 *
 * @param[in,out] Address Destination address in internal flash.
 * @param[in] array Source words to program.
 * @param[in] words Number of 32-bit words to program.
 * @return 0 on success, or the STM32 flash status error mask.
 */
uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words) {
    for (int i = 0; i < words; i += 2) {
      FLASH_Program_DoubleWord(&Address[i], array[i], array[i+1]);
      if (flash_err) return flash_err;
    }
    return 0;
}
/** @} */
