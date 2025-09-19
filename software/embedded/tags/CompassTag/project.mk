# List of all the project related files.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/CompassTagv1/board.mk

ALLCSRC += \
       ak09940a.c \
       config.c \
       datalog.c \
       default_config.c \
       handlers.c  \
       lis2du12.c \
       main.c \
       monitor.c \
       mx25r.c \
       pb_common.c \
       pb_decode.c \
       pb_encode.c  \
       persistent.c \
       pwr.c \
       rtc_rv3028.c \
       sensors.c \
       state_machine.c  \
       state_run.c \
       stm32adc.c \
       stm32flash.c \
       tag.pb.c \
       tagdata.pb.c \
       test.c   \
       time.c        
