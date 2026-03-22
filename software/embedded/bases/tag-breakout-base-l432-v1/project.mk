# List of all the project related files.

include $(BOARDDIR)/tag-breakout-base-l432-v1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk
CXXFLAGS += -DUSEEPRINTF
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd.c \
	   stlink.c \
	   stm32adc.c

