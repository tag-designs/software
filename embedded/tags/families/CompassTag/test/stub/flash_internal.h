#ifndef STUB_FLASH_INTERNAL_H
#define STUB_FLASH_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
void FLASH_Lock(void);
void FLASH_Unlock(void);
void FLASH_Flush_Data_Cache(void);
uint32_t FLASH_Read_Checked(const void *Address, void *Data, size_t Bytes);
uint32_t FLASH_Program_Array(uint32_t *Address, uint32_t *array, int words);
#endif
