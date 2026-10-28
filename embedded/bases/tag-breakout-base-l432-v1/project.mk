# List of all the project related files.

include $(BOARDDIR)/Tag_Breakout_Base_L432_V1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

# The base has a USB virtual serial port for its error log (debug_printf in
# src/usbcfg.c, which drops lines rather than block while no host reads
# them). Logging is off; to turn it on, for SWD fault logging as used in
# design/investigations/2026-10-0x81-dbg-sleep.md, add:
#   PROJECT_UDEFS = -DUSEEPRINTF=1 -DUSE_CDC_EPRINTF=1
CXXFLAGS += -DUSEEPRINTF
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd.c \
	   stlink.c \
	   stm32adc.c
