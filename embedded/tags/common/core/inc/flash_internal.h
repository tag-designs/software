/**
 * @file flash_internal.h
 * @brief Low-level STM32 internal flash erase and program helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_FLASH_INTERNAL_H
#define TAG_CORE_FLASH_INTERNAL_H

#include <stdint.h>

/** @name Internal flash programming
 * Helpers used by persistent storage code to update flash records while
 * centralizing STM32 unlock, erase, program, and cache-flush behavior.
 * @{
 */
/**
 * @brief Lock internal flash control registers against accidental writes.
 */
void FLASH_Lock(void);

/**
 * @brief Unlock internal flash control registers for an erase or program step.
 */
void FLASH_Unlock(void);

/**
 * @brief Erase one internal flash page.
 *
 * @param[in] Page Flash page number to erase.
 */
void FLASH_PageErase(uint32_t Page);

/**
 * @brief Program one aligned STM32 double-word.
 *
 * @param[in,out] Address Aligned destination address in internal flash.
 * @param[in] Data0 Lower 32 bits to program.
 * @param[in] Data1 Upper 32 bits to program.
 */
void FLASH_Program_DoubleWord(uint32_t *Address, uint32_t Data0,
                              uint32_t Data1);

/**
 * @brief Flush the STM32 data cache after flash contents change.
 */
void FLASH_Flush_Data_Cache(void);

/**
 * @brief Program an array of 32-bit words as aligned flash double-words.
 *
 * @param[in,out] Address Destination address in internal flash.
 * @param[in] array Source words to program.
 * @param[in] words Number of 32-bit words to program.
 * @return 0 on success, or the STM32 flash status error mask.
 */
uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words);
/** @} */

#endif
