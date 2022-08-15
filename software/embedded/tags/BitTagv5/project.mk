# List of all the project related files.

include $(BOARDDIR)/BitTagv5/board.mk

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
       rtc_rv8803.c \
       state_machine.c  \
       stm32adc.c \
       stm32flash.c \
       tag.pb.c \
       tagdata.pb.c \
       test.c \
       time.c        
