# List of all the project related files.

include $(BOARDDIR)/Tag_Breakout_Base_L432_V1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

# The base's error log goes to its USB virtual serial port (debug_printf in
# src/usbcfg.c, which drops lines rather than block while no host reads them).
PROJECT_UDEFS = -DUSEEPRINTF=1 -DUSE_CDC_EPRINTF=1
CXXFLAGS += -DUSEEPRINTF
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd.c \
	   stlink.c \
	   stm32adc.c
