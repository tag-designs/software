# RV3028_UIUCTag: the read-only RV3028 register probe for the UIUCTag board.
# The source is ../common/src/rv3028_probe.c; see ../RV3028_PresTagv3/README.md.
#
# UIUCTag's firmware defines SWAP_I2C: the line labelled RTC_SCL (PB7) is SDA
# and RTC_SDA (PB6) is SCL.

LOADER_BOARD_INC = ../../boards/UIUCTag/generated

LOADER_MCU_CFG_DIR = ../common/cfg/stm32l4

LOADER_COMMON_CSRC = \
       loader_clock.c \
       loader_delay.c

LOADER_CSRC = \
       rv3028_probe.c

UDEFS += -DPROBE_SWAP_I2C=1
