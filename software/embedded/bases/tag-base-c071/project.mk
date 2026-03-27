# List of all the project related files.

include $(BOARDDIR)/tag-base-c071-v1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk
CXXFLAGS += -DLINE_TAG_SWCLK=LINE_SPI1_SCK -DLINE_TAG_SWDIO=LINE_SPI1_MOSI -DLINE_TAG_SWDIO_IN=LINE_SPI2_MOSI
ALLCSRC += \
       main.c \
	   usbcfg.c \
	   ll_swd_spi.c \
	   stlink-local.c \
	   stm32adc.c

