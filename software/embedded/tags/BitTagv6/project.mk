# List of all the project related files.
#USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

ALLCSRC += \
       ADXL362.c \
       bt_config.c \
       bt_monitor.c \
       bt_persistent.c \
       bt_pwr.c \
       bt_state_run.c \
       default_config.c \
       handlers.c  \
       main.c \
       pb_common.c \
       pb_decode.c \
       pb_encode.c  \
       rtc_rv3028.c \
       state_machine.c  \
       stm32adc.c \
       stm32flash.c \
       tag.pb.c \
       tagdata.pb.c \
       test.c   \
       time.c        
