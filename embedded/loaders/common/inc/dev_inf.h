/**
 * @file    dev_inf.h
 * @brief   STM32CubeProgrammer external-loader device descriptor.
 *
 * @details The layout is fixed by STM32CubeProgrammer, which reads the global
 *          symbol @c StorageInfo out of the loader ELF by name. It matches
 *          ST's Dev_Inf.h; do not reorder or resize fields. Each target
 *          defines StorageInfo in its own dev_inf.c.
 */

#ifndef DEV_INF_H
#define DEV_INF_H

/** @name Device types understood by STM32CubeProgrammer
 * @{
 */
#define MCU_FLASH   1  ///< On-chip flash.
#define NAND_FLASH  2  ///< NAND flash.
#define NOR_FLASH   3  ///< NOR flash; what ST's own SPI NOR loaders declare.
#define SRAM        4  ///< External SRAM.
#define PSRAM       5  ///< External PSRAM.
#define PC_CARD     6  ///< PC card.
#define SPI_FLASH   7  ///< SPI flash (unused by ST's SPI NOR loaders).
#define I2C_FLASH   8  ///< I2C flash.
#define SDRAM       9  ///< External SDRAM.
#define I2C_EEPROM  10 ///< I2C EEPROM.
/** @} */

/** @brief Maximum number of sector-size groups in a descriptor. */
#define SECTOR_NUM  10

/**
 * @struct  DeviceSectors
 * @brief   One group of equally sized sectors; a zero entry ends the list.
 */
struct DeviceSectors {
  unsigned long SectorNum;  ///< Number of sectors in the group.
  unsigned long SectorSize; ///< Size of each sector in bytes.
};

/**
 * @struct  StorageInfo
 * @brief   Descriptor STM32CubeProgrammer reads to learn the memory's shape.
 */
struct StorageInfo {
  char DeviceName[100];              ///< Shown in the programmer's UI.
  unsigned short DeviceType;         ///< One of the device types above.
  unsigned long DeviceStartAddress;  ///< Base address the programmer uses.
  unsigned long DeviceSize;          ///< Total size in bytes.
  unsigned long PageSize;            ///< Programming page size in bytes.
  unsigned char EraseValue;          ///< Content of erased memory.
  struct DeviceSectors sectors[SECTOR_NUM]; ///< Sector map.
};

/**
 * @def     LOADER_DEV_INFO
 * @brief   Placement for the one StorageInfo definition in a target.
 *
 * @details Keeps the descriptor out of --gc-sections and in the section the
 *          loader link map retains.
 */
#define LOADER_DEV_INFO __attribute__((used, section(".dev_info")))

#endif /* DEV_INF_H */
