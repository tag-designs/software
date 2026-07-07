# List of all the project related files.

include $(BOARDDIR)/Tag_Breakout_Base_L432_U375_1V8/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

SWD_DELAY_COUNT ?= 1
SWD_SPI_BR ?= (2U << 3)

ALLCSRC += \
       main.c \
       usbcfg.c \
       ll_swd_spi.c \
       stlink.c
