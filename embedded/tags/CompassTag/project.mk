# CompassTag build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/CompassTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_mx25r \
       sensor_mag_ak09940a

include ../common/modules/modules.mk

# Tag-local application and sensor sources. Local test.c shadows the shared
# test module through VPATH.
ALLCSRC += \
       config.c \
       datalog.c \
       lis2du12.c \
       sensors.c \
       state_run.c
