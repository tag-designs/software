# IMUTagBreakout build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/IMUTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       debug_log \
       tag_test \
       rtc_rv3028 \
       flash_mx25l

include ../common/modules/modules.mk

UDEFS += -DTAG_SENSOR_MAG_AK09940A=1 -DTAG_SENSOR_PRESSURE_LPS22HH=1

# Tag-local application and sensor sources. Sensor self-test hooks live beside
# the local drivers and are called by the shared test module.
ALLCSRC += \
       ak09940.c \
       ak09940_test.c \
       config.c \
       datalog.c \
       lps22hh.c \
       lps22hh_test.c \
       sensors.c \
       state_run.c \
       stubs.c      
