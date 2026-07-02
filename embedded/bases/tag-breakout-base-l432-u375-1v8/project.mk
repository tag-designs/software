# List of all the project related files.

include $(BOARDDIR)/Tag_Breakout_Base_L432_U375_1V8/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

ALLCSRC += \
       main.c \
       usbcfg.c \
       ll_swd.c \
       stlink.c
