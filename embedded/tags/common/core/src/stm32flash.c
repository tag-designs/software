/**
 * @file stm32flash.c
 * @brief Low-level STM32 internal flash erase and program helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include <stdbool.h>
#include <string.h>

#include "flash_internal.h"

#define FLASH_KEY1 ((uint32_t)0x45670123U) /*!< Flash key1 */
#define FLASH_KEY2 \
  ((uint32_t)0xCDEF89ABU) /*!< Flash key2: used with FLASH_KEY1 */

#if !defined(FLASH_SR_MISERR)
#define FLASH_SR_MISERR 0U
#endif
#if !defined(FLASH_SR_FASTERR)
#define FLASH_SR_FASTERR 0U
#endif
#if !defined(FLASH_SR_RDERR)
#define FLASH_SR_RDERR 0U
#endif
#if !defined(FLASH_SR_OPTVERR)
#define FLASH_SR_OPTVERR 0U
#endif
#if !defined(FLASH_SR_OPTWERR)
#define FLASH_SR_OPTWERR 0U
#endif

#if defined(FLASH_ECCR_ECCD)
#define TAG_FLASH_ECC_DETECTED_BIT FLASH_ECCR_ECCD
#elif defined(FLASH_ECCDR_ECCD)
#define TAG_FLASH_ECC_DETECTED_BIT FLASH_ECCDR_ECCD
#else
#define TAG_FLASH_ECC_DETECTED_BIT FLASH_READ_ERROR_INVALID_ADDRESS
#endif

#define FLASH_STATUS_ERROR_MASK (FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR | \
                                 FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | \
                                 FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
                                 FLASH_SR_RDERR | FLASH_SR_OPTVERR | FLASH_SR_OPTWERR)

#if defined(TAG_STM32U3_FLASH) && TAG_STM32U3_FLASH
#define TAG_INTERNAL_FLASH_PAGE_SIZE 4096U
#define TAG_FLASH_PROGRAM_ROW_WORDS 4U
#define TAG_FLASH_PROGRAM_ROW_ALIGN 16U
#else
#define TAG_INTERNAL_FLASH_PAGE_SIZE 2048U
#define TAG_FLASH_PROGRAM_ROW_WORDS 2U
#define TAG_FLASH_PROGRAM_ROW_ALIGN 8U
#endif

#define TAG_INTERNAL_FLASH_BASE 0x08000000U

extern void _unhandled_exception(void);

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
  return FLASH->SR & FLASH_STATUS_ERROR_MASK;
}

/**
 * @brief Read the sticky flash ECC error flags.
 */
static inline uint32_t FLASH_EccErrors(void) {
  uint32_t errors = 0U;
#if defined(FLASH_ECCR_ECCC)
  errors |= FLASH->ECCR & FLASH_ECCR_ECCC;
#endif
#if defined(FLASH_ECCR_ECCD)
  errors |= FLASH->ECCR & FLASH_ECCR_ECCD;
#endif
#if defined(FLASH_ECCCR_ECCC)
  errors |= FLASH->ECCCR & FLASH_ECCCR_ECCC;
#endif
#if defined(FLASH_ECCDR_ECCD)
  errors |= FLASH->ECCDR & FLASH_ECCDR_ECCD;
#endif
  return errors;
}

/**
 * @brief Clear all sticky flash status and ECC error flags before an operation.
 */
void FLASH_ClearEccErrors(void) {
#if defined(FLASH_ECCR_ECCC)
  SET_BIT(FLASH->ECCR, FLASH_ECCR_ECCC);
#endif
#if defined(FLASH_ECCR_ECCD)
  SET_BIT(FLASH->ECCR, FLASH_ECCR_ECCD);
#endif
#if defined(FLASH_ECCCR_ECCC)
  SET_BIT(FLASH->ECCCR, FLASH_ECCCR_ECCC);
#endif
#if defined(FLASH_ECCDR_ECCD)
  SET_BIT(FLASH->ECCDR, FLASH_ECCDR_ECCD);
#endif
}

static inline void FLASH_ClearAllErrors(void) {
  SET_BIT(FLASH->SR, FLASH_STATUS_ERROR_MASK);

  FLASH_ClearEccErrors();
}
/** @} */

uint32_t flash_err = 0;
volatile uint32_t flash_ecc_flags = 0;
volatile uint32_t flash_ecc_eccr = 0;
static volatile bool flash_ecc_probe_active = false;
static volatile bool flash_ecc_probe_failed = false;

/**
 * @brief Convert expected flash ECC NMIs into checked-read failures.
 */
void NMI_Handler(void) {
  uint32_t eccr = FLASH_EccErrors();
  uint32_t ecc_errors = eccr;

  if (ecc_errors) {
    flash_ecc_flags |= ecc_errors;
    flash_ecc_eccr = eccr;
    FLASH_ClearEccErrors();

    if (flash_ecc_probe_active) {
      flash_ecc_probe_failed = true;
      return;
    }
  }

  _unhandled_exception();
}

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
  uint32_t page_field = Page;
  uint32_t cr_clear = FLASH_CR_PNB;
  uint32_t cr_set;

#if defined(FLASH_CR_BKER)
  const uint32_t pages_per_bank =
      FLASH_CR_PNB >> POSITION_VAL(FLASH_CR_PNB);
  const uint32_t bank = Page / (pages_per_bank + 1U);

  if (bank > 1U) {
    flash_err = FLASH_SR_PGAERR;
    return;
  }

  page_field = Page % (pages_per_bank + 1U);
  cr_clear |= FLASH_CR_BKER;
#endif

  __disable_irq();
  FLASH_ClearAllErrors();

  cr_set = page_field << POSITION_VAL(FLASH_CR_PNB);
#if defined(FLASH_CR_BKER)
  if (Page / (pages_per_bank + 1U)) {
    cr_set |= FLASH_CR_BKER;
  }
#endif
  MODIFY_REG(FLASH->CR, cr_clear, cr_set);
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  SET_BIT(FLASH->CR, FLASH_CR_STRT);

  while (FLASH->SR & FLASH_SR_BSY)
    ;

  CLEAR_BIT(FLASH->CR, FLASH_CR_PER);
  __enable_irq();
  flash_err = FLASH_Errors();
}

uint32_t FLASH_PageSize(void) {
  return TAG_INTERNAL_FLASH_PAGE_SIZE;
}

void FLASH_PageEraseAddress(uint32_t Address) {
  FLASH_PageErase((Address - TAG_INTERNAL_FLASH_BASE) / FLASH_PageSize());
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
  uint32_t row[TAG_FLASH_PROGRAM_ROW_WORDS];

  row[0] = Data0;
  row[1] = Data1;
#if TAG_FLASH_PROGRAM_ROW_WORDS > 2U
  for (uint32_t i = 2U; i < TAG_FLASH_PROGRAM_ROW_WORDS; i++) {
    row[i] = 0xffffffffU;
  }
#endif

  FLASH_Program_Array(Address, row, TAG_FLASH_PROGRAM_ROW_WORDS);
}

static void FLASH_Program_Row(uint32_t *Address, const uint32_t *Data) {
  if (((uint32_t)Address) & (TAG_FLASH_PROGRAM_ROW_ALIGN - 1U)) {
    flash_err = FLASH_SR_PGAERR;
    return;
  }

  FLASH_ClearAllErrors();

  __disable_irq();

  SET_BIT(FLASH->CR, FLASH_CR_PG);

  for (uint32_t i = 0U; i < TAG_FLASH_PROGRAM_ROW_WORDS; i++) {
    ((__IO uint32_t *)Address)[i] = Data[i];
  }
  __DSB();

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
#if defined(FLASH_ACR_DCEN) && defined(FLASH_ACR_DCRST)
  if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN) != RESET) {
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);  // disable data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCRST);   // reset data cache
    SET_BIT(FLASH->ACR, FLASH_ACR_DCEN);    // enable data cache
  }
#endif
}

uint32_t FLASH_Read_DoubleWord_Checked(const uint64_t *Address, uint64_t *Data) {
  if ((Address == NULL) || (Data == NULL) || (((uintptr_t)Address) & 0x7U)) {
    return FLASH_READ_ERROR_INVALID_ADDRESS;
  }

  flash_ecc_flags = 0;
  flash_ecc_eccr = 0;
  flash_ecc_probe_failed = false;
  FLASH_ClearEccErrors();

  flash_ecc_probe_active = true;
  __DSB();
  uint64_t value = *(volatile const uint64_t *)Address;
  __DSB();
  flash_ecc_probe_active = false;

  uint32_t ecc_errors = flash_ecc_flags | FLASH_EccErrors();
  if (ecc_errors) {
    FLASH_ClearEccErrors();
  }

  if (flash_ecc_probe_failed || ecc_errors) {
    return ecc_errors ? ecc_errors : TAG_FLASH_ECC_DETECTED_BIT;
  }

  *Data = value;
  return 0;
}

uint32_t FLASH_Read_Checked(const void *Address, void *Data, size_t Bytes) {
  if ((Address == NULL) || (Data == NULL) || (((uintptr_t)Address) & 0x7U) ||
      ((Bytes & 0x7U) != 0U)) {
    return FLASH_READ_ERROR_INVALID_ADDRESS;
  }

  const uint64_t *source = (const uint64_t *)Address;
  uint8_t *dest = (uint8_t *)Data;

  for (size_t offset = 0; offset < Bytes; offset += sizeof(uint64_t)) {
    uint64_t word;
    uint32_t error = FLASH_Read_DoubleWord_Checked(source++, &word);
    if (error) {
      return error;
    }
    memcpy(&dest[offset], &word, sizeof(word));
  }

  return 0;
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
    if ((((uint32_t)Address) & (TAG_FLASH_PROGRAM_ROW_ALIGN - 1U)) != 0U) {
      flash_err = FLASH_SR_PGAERR;
      return flash_err;
    }

    for (int i = 0; i < words; i += TAG_FLASH_PROGRAM_ROW_WORDS) {
      uint32_t row[TAG_FLASH_PROGRAM_ROW_WORDS];

      for (uint32_t j = 0U; j < TAG_FLASH_PROGRAM_ROW_WORDS; j++) {
        int index = i + (int)j;
        row[j] = (index < words) ? array[index] : 0xffffffffU;
      }

      FLASH_Program_Row(&Address[i], row);
      if (flash_err) return flash_err;
    }
    return 0;
}
/** @} */
