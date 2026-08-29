# GD5F2GM7RE SPI-NAND external flash driver.
#
# Reuses the common GD5F SPI-NAND driver with the 2 Gbit geometry and identity
# bytes from the GD5F2GM7REYIGR datasheet. The logical block count is the
# datasheet minimum-valid-block floor so host-reported capacity stays within
# guaranteed usable media.

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk

# Feature and ID defines select the shared GD5F path and expected JEDEC-style
# identity bytes returned by the flash self-test.
UDEFS += -DTAG_HAS_EXTERNAL_FLASH=1 -DTAG_FLASH_GD5F2GM7RE=1
# Keep GD5F2 deep power-down off during pressure/collection bring-up. The
# shared driver has a separate non-RUNNING deep-sleep hook, but entering `B9h`
# still needs more board testing before it becomes the default for this target.
UDEFS += -DTAG_GD5F_DEEP_POWER_DOWN=0
UDEFS += -DTAG_GD5F_RELEASE_DEEP_POWER_DOWN=1
UDEFS += -DGD5F_ID_MANUFACTURER=0xC8U
UDEFS += -DGD5F_ID_DEVICE=0x82U
# NAND geometry exposed to gd5f.c and the storage layer.
UDEFS += -DGD5F_PAGE_SIZE=2048UL
UDEFS += -DGD5F_SPARE_SIZE=128UL
UDEFS += -DGD5F_PAGES_PER_BLOCK=64UL
UDEFS += -DGD5F_PHYSICAL_BLOCK_COUNT=2048UL
UDEFS += -DGD5F_MIN_VALID_BLOCK_COUNT=2008UL
UDEFS += -DGD5F_LOGICAL_BLOCK_COUNT=2008UL

# The shared flash test now treats all TAG_FLASH_GD5F* variants as SPI-NAND.
ALLCSRC += \
       gd5f.c \
       storage_flash.c \
       external_flash_test.c
