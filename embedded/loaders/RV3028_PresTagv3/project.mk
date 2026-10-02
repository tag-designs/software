# RV3028_PresTagv3: an SRAM probe that reads the RV3028 RTC's registers over
# I2C, for a tag that has no firmware or cannot run it. Not a flash loader: it
# keeps the loader framework's clock and delay code and nothing else.
#
# PresTagv3 and CompassTagv1 both wire the RV3028 to PB6 (SDA) and PB7 (SCL),
# so the image works on either. The source is ../common/src/rv3028_probe.c.

LOADER_BOARD_INC = ../../boards/PresTagv3/generated

LOADER_MCU_CFG_DIR = ../common/cfg/stm32l4

LOADER_COMMON_CSRC = \
       loader_clock.c \
       loader_delay.c

LOADER_CSRC = \
       rv3028_probe.c
