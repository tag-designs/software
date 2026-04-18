# List of all the project related files.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/CompassTagv1/board.mk

ALLCSRC += \
       handlers.c \
       main.c \
       rtc_rv3028.c \
       time.c        
