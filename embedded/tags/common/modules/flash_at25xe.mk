# AT25-series external flash driver.

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk

UDEFS += -DTAG_HAS_EXTERNAL_FLASH=1 -DTAG_FLASH_AT25XE=1

ALLCSRC += \
       at25xe.c \
       storage_flash.c \
       external_flash_test.c
