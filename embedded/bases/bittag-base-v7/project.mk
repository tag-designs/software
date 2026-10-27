# List of all the project related files.

include $(CHIBIOS)/os/hal/lib/streams/streams.mk

include $(BOARDDIR)/BITTAG_BASE_V7/board.mk
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd.c \
	   stlink.c \
	   stm32adc.c
