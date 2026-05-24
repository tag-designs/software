# List of all the project related files.

include $(BOARDDIR)/BITTAG_BASE_V7/board.mk
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd.c \
	   stlink.c \
	   stm32adc.c
