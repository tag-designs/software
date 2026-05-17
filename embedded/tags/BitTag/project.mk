# BitTag build manifest.
#USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       sensor_accel_adxl362

include ../common/modules/modules.mk

# Historical BitTag runtime. These files predate the generic monitor/power/log
# structure and are kept local to BitTag so the shared common modules describe
# only code used across multiple current tag families. BitTag's local monitor.c
# overrides the generic common/core/src/monitor.c contributed by tag_core.
ALLCSRC += \
       bt_config.c \
       bt_persistent.c \
       bt_pwr.c \
       bt_state_run.c
