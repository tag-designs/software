# AT25XE_CompassTagv1 external loader.
#
# Reads -- and, in the -RW build, erases and programs -- the AT25XE321D on the
# CompassTagv1 board, which CompassTagAT25 and CompassTagAT25Breakout run on.
# One loader per board, not per tag: the image is bound to the board's pins,
# flash part and MCU.

# Committed ChibiOS board description; only board.h is used, not board.c.
LOADER_BOARD_INC = ../../boards/CompassTagv1/generated

# STM32L432 defaults from ../common/cfg/stm32l4; ./cfg would override them.
LOADER_MCU_CFG_DIR = ../common/cfg/stm32l4

LOADER_CSRC = \
       at25xe_loader.c \
       board_loader.c \
       dev_inf.c
