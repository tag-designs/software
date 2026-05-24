# BitTag build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       sensor_accel_adxl362

include ../common/modules/modules.mk

# BitTag keeps only its activity-log application code locally. Core runtime,
# monitor, persistence, power, RTC, and ADXL362 driver sources come from the
# common modules above.
ALLCSRC += \
       bt_config.c \
       bt_state_run.c \
       datalog.c \
       devices.c
