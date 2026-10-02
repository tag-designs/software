# RV3028_IMUTagNandv2: the read-only RV3028 register probe for the
# IMUTagNandv2 board (STM32U375). The source is ../common/src/rv3028_probe.c;
# see ../RV3028_PresTagv3/README.md.
#
# The board names its I2C1 pins LINE_SDA (PB7) and LINE_SCL (PB6); the probe
# drives them as GPIO, bit-banged, exactly as on the L432 boards. No swap.

LOADER_BOARD_INC = ../../boards/IMUTagNandv2/generated

LOADER_MCU_CFG_DIR = ../common/cfg/stm32u3

LOADER_COMMON_CSRC = \
       loader_clock_u3.c \
       loader_delay.c

LOADER_CSRC = \
       rv3028_probe.c

UDEFS += -DLINE_RTC_SDA=LINE_SDA -DLINE_RTC_SCL=LINE_SCL
