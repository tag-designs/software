# IMUTagBreakout build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/IMUTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       debug_log \
       tag_test \
       rtc_rv3028 \
       flash_mx25l \
       sensor_pressure_lps22hh \
       sensor_mag_ak09940a

include ../common/modules/modules.mk

# Tag-local application and sensor sources. Sensor self-test hooks live beside
# the local drivers and are called by the shared test module.
ALLCSRC += \
       config.c \
       datalog.c \
       devices.c \
       lps22hh_test.c \
       sensors.c \
       state_run.c \
       stubs.c      
