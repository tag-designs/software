/**
 * @file flash_internal.h
 * @brief Low-level STM32 internal flash erase and program helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_FLASH_INTERNAL_H
#define TAG_CORE_FLASH_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FLASH_READ_ERROR_INVALID_ADDRESS (1UL << 31)

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
 * @brief Return the STM32 internal flash erase page size in bytes.
 */
uint32_t FLASH_PageSize(void);

/**
 * @brief Erase the internal flash page containing an absolute flash address.
 *
 * @param[in] Address Address in STM32 internal flash.
 */
void FLASH_PageEraseAddress(uint32_t Address);

/*
 * Pull in the per-target configuration before applying the default below.
 * Without this the default wins in any translation unit that reaches this header
 * first, leaving some objects compiled with the flag clear and others with it
 * set -- an inconsistency that would place sconfig differently per object.
 */
#include "custom.h"

#ifndef TAG_STORED_CONFIG_OWN_PAGE
/**
 * @def TAG_STORED_CONFIG_OWN_PAGE
 * @brief Set when the linker gives the stored configuration its own flash page.
 *
 * @details STM32 flash programming can only clear bits, so rewriting a
 *          provisioned configuration requires erasing it first, and an erase
 *          takes the whole page. Where the configuration shares a page with
 *          sEpoch and the internal checkpoint headers, that erase is impossible
 *          and the configuration can only be written into an already-erased
 *          region.
 *
 *          Targets that reserve a dedicated page set this to 1 and gain
 *          erase-before-write plus enforcement of the idle-implies-clean
 *          invariant. It defaults to 0 so flash-constrained targets keep their
 *          existing layout and behaviour, spending neither a page nor the code.
 */
#define TAG_STORED_CONFIG_OWN_PAGE 0
#endif

#if TAG_STORED_CONFIG_OWN_PAGE
/** @brief Dedicated, independently erasable section for the stored config. */
#define TAG_STORED_CONFIG_SECTION ".storedconfig"
#else
/** @brief Stored config shares the general persistent region. */
#define TAG_STORED_CONFIG_SECTION ".persistent"
#endif

/**
 * @brief Report whether the state-transition marker log is empty.
 *
 * @details The firmware holds IDLE => empty state log as an invariant. Any code
 *          that sets TagState_IDLE without going through Idle() -- boot cleanup
 *          being the live example -- must check this first, or it claims idle
 *          over a log that still describes a completed run and the host erase
 *          path, which only runs from FINISHED or ABORTED, never reclaims it.
 *
 * @return true when the first marker slot reads erased; false when a marker is
 *         present or the region cannot be read.
 *
 * @see persistentIdleStateClean()
 */
bool stateLogEmpty(void);

#if TAG_STORED_CONFIG_OWN_PAGE
/**
 * @brief Report whether the provisioned configuration region reads as erased.
 *
 * @return true when every byte of the stored configuration is 0xFF; false when
 *         any byte is programmed or the region cannot be read.
 */
bool storedConfigErased(void);

/**
 * @brief Report whether persistent state is clean enough to claim idle.
 *
 * @details The invariant is idle => clean state and configuration, which is what
 *          erasePersistent() leaves behind. Several paths reach idle without
 *          erasing -- a reflash preserves the persistent region by design, and
 *          both SelfTest() and reset recovery arrive at idle directly -- so the
 *          condition has to be checked rather than assumed. Powering up onto
 *          stale contents aborts instead of claiming idle.
 *
 *          Only meaningful where TAG_STORED_CONFIG_OWN_PAGE is set: enforcing
 *          the invariant requires a write path able to restore cleanliness.
 *
 * @return true when both the stored configuration and the state marker log are
 *         erased.
 */
bool persistentIdleStateClean(void);
#endif

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
 * @brief Clear sticky flash ECC error flags.
 */
void FLASH_ClearEccErrors(void);

/**
 * @brief Read one flash double-word while converting ECC NMIs to errors.
 *
 * @param[in] Address Aligned flash source address.
 * @param[out] Data Destination for the read value.
 * @return 0 on success, or a flash/ECC error mask.
 */
uint32_t FLASH_Read_DoubleWord_Checked(const uint64_t *Address, uint64_t *Data);

/**
 * @brief Read aligned flash bytes while converting ECC NMIs to errors.
 *
 * @param[in] Address Aligned flash source address.
 * @param[out] Data Destination buffer.
 * @param[in] Bytes Number of bytes to read; must be a multiple of 8.
 * @return 0 on success, or a flash/ECC error mask.
 */
uint32_t FLASH_Read_Checked(const void *Address, void *Data, size_t Bytes);

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
