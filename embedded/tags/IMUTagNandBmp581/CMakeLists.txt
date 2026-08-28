
#[[
  @brief  Defines the IMUTagNandBmp581 embedded firmware target.

  @details Builds the STM32U375 IMUTag replacement firmware against the shared
           IMUTag protobuf identity while selecting the generated
           IMUTagNandv2 board files. The target keeps the IMUTAG protocol
           identifier so host tools treat this hardware revision as a
           replacement for IMUTagNand.
]]
add_embedded_target(IMUTagNandBmp581 imutag_proto TAG_FLASH_SIZE=1024K)
add_dependencies(IMUTagNandBmp581 board-imutag-nand-v2)
