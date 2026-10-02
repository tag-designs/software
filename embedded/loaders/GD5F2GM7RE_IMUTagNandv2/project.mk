# GD5F2GM7RE_IMUTagNandv2 external loader (STM32U375).
#
# Reads the GD5F2GM7RE SPI NAND on the IMUTagNandv2 board, which
# IMUTagNandBmp581 runs on. Read-only: no erase or program code, and no
# NAND reset or block-lock write (see ../design/u375-nand-loader-plan.md).

LOADER_BOARD_INC = ../../boards/IMUTagNandv2/generated

LOADER_MCU_CFG_DIR = ../common/cfg/stm32u3

LOADER_CSRC = \
       gd5f_loader.c \
       board_loader.c \
       dev_inf.c

# Part identity and geometry: the same values as
# tags/common/modules/flash_gd5f2gm7re.mk. Keep the two in step.
UDEFS += -DGD5F_ID_MANUFACTURER=0xC8U -DGD5F_ID_DEVICE=0x82U
UDEFS += -DGD5F_PAGE_SIZE=2048UL -DGD5F_SPARE_SIZE=128UL
UDEFS += -DGD5F_PAGES_PER_BLOCK=64UL -DGD5F_PHYSICAL_BLOCK_COUNT=2048UL

# A paged part: Serve() answers LOADER_CMD_READ_PAGE (whole pages, raw or ECC).
UDEFS += -DLOADER_FLASH_PAGED=1
