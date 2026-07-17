# GD5F1GQ5RE SPI-NAND external flash driver.

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk

UDEFS += -DTAG_HAS_EXTERNAL_FLASH=1 -DTAG_FLASH_GD5F1GQ5RE=1

ALLCSRC += \
       gd5f1gq5re.c \
       storage_flash.c \
       external_flash_test.c
