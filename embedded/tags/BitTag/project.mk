# BitTag build manifest.
#USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test

include ../common/modules/modules.mk

UDEFS += -DTAG_SENSOR_ACCEL_ADXL362=1
UDEFS += -DTAG_RTC_RV3028=1

# Historical BitTag runtime. These files predate the generic monitor/power/log
# structure and are kept local to BitTag so the shared common modules describe
# only code used across multiple current tag families. BitTag's local monitor.c,
# persistent.c, pwr.c, main.c, time.c, state_machine.c, ADXL362 driver, and
# RV3028 RTC files override or freeze code that is generic for newer tags but
# historically part of BitTag firmware.
ALLCSRC += \
       ADXL362.c \
       adxl362_test.c \
       bt_config.c \
       bt_state_run.c \
       rtc_rv3028.c \
       rtc_test.c
