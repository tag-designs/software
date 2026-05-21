# MX25L external flash driver.

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk

UDEFS += -DTAG_HAS_EXTERNAL_FLASH=1 -DTAG_FLASH_MX25L=1

ALLCSRC += \
       mx25l.c \
       storage_flash.c \
       external_flash_test.c
