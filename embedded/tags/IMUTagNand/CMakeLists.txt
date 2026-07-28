
add_embedded_target(IMUTagNand imutag_proto TAG_FLASH_SIZE=1024K)
add_dependencies(IMUTagNand board-imutag-nand-v1)
