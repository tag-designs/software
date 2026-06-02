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
       sensor_mag_ak09940a \
       sensor_imu_lsm6dsv16x

include ../common/modules/modules.mk

# Tag-local application sources. Shared modules provide core runtime, RTC,
# storage, pressure, and magnetometer drivers; IMUTagBreakout keeps only the
# LPS22HH self-test hook locally until the pressure module grows a generic
# concrete test.
ALLCSRC += \
       config.c \
       datalog.c \
       devices.c \
       sensors.c \
       state_run.c \
       stubs.c      
