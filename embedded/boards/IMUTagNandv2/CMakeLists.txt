#[[
  @brief  Generates ChibiOS board files for the IMUTagNand BMP581 breakout.

  @details The generated board target maps the STM32U375 package pins to the
           replacement breakout nets, including the separate BMP581 SPI bus and
           GD5F2GM7RE flash control lines. The generated line names are the
           firmware source of truth for IMUTagNandBmp581.
]]
generate_configured_board_files(board-imutag-nand-v2
  PROCESSOR stm32u3xx
  BOARD_TYPE IMUTagNandv2
  CUSTOMIZATIONS cfg/board-customizations.json)
