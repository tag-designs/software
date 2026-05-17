# MX25R external flash driver.

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk

UDEFS += -DTAG_HAS_EXTERNAL_FLASH=1 -DTAG_FLASH_MX25R=1 -DEXTERNAL_FLASH=1

ALLCSRC += \
       mx25r.c
