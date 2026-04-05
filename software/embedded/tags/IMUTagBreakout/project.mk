# List of all the project related files.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/IMUTagv1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

#ak09940a.c 
# mx25l.c 

ALLCSRC += \
       config.c \
       datalog.c \
       default_config.c \
       handlers.c  \
       lps22hh.c \
       main.c \
       mx25l.c \
       monitor.c pb_common.c \
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
       time.c  \
       stubs.c      
