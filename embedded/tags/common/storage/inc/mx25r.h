#ifndef _MX25R_H_
#define _MX25R_H_

#include "storage_device.h"

#define MX25R_SIZE                            (1024UL * 1024UL * 4UL)
#define MX25R_SECTOR_SIZE                     (4096UL)
#define MX25R_SECTOR_COUNT                    (MX25R_SIZE / MX25R_SECTOR_SIZE)

extern const TagStorageOps mx25rStorageOps;

#endif 
