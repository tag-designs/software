# CompassTagAT25Breakout build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/CompassTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_mag_ak09940a

include ../common/modules/modules.mk

# Tag-local application and sensor sources. Local state_machine.c and test.c
# shadow the shared modules through VPATH while this target is being split out.
ALLCSRC += \
       config.c \
       datalog.c \
       lis2du12.c \
       sensors.c \
       state_run.c
