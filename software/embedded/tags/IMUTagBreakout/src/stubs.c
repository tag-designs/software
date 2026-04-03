#include <stdint.h>
#include <stdbool.h>

void ExFlashPwrDown(void){}
void ExFlashPwrUp(void){}

// Check external id, -1 if error
// otherwise size in kb

int ExCheckID(void){return 0;}
int ExSectorSize(void){return 0;}
int ExSectorCount(void){return 0;}

bool ExFlashWrite(uint32_t address, uint8_t *buf, int *cnt){return false;}
bool ExFlashSectorErase(uint32_t address){return false;}
void ExFlashRead(uint32_t address, uint8_t *buf, int num){}

void accelDeinit(void) {}
void accelInit(void){}
bool accelTest(void){return false;}