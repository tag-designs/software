#ifndef TAG_CORE_FLASH_INTERNAL_H
#define TAG_CORE_FLASH_INTERNAL_H

#include <stdint.h>

void FLASH_Lock(void);
void FLASH_Unlock(void);
void FLASH_PageErase(uint32_t Page);
void FLASH_Program_DoubleWord(uint32_t *Address, uint32_t Data0,
                              uint32_t Data1);
void FLASH_Flush_Data_Cache(void);
uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words);

#endif
