#ifndef _EXTERNAL_FLASH_H_
#define _EXTERNAL_FLASH_H_

void FlashSpiOff(void);
void FlashSpiOn(void);

void ExFlashPwrDown(void);
void ExFlashPwrUp(void);

// Check external id, -1 if error
// otherwise size in kb

int ExCheckID(void);
int ExSectorSize(void);
int ExSectorCount(void);

bool ExFlashWrite(uint32_t address, uint8_t *buf, int *cnt);
bool ExFlashSectorErase(uint32_t address);
void ExFlashRead(uint32_t address, uint8_t *buf, int num);

#endif 