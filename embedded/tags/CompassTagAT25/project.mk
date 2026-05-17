# CompassTagAT25 build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/CompassTagv1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       monitor \
       rtc_rv3028 \
       flash_at25xe \
       storage_persistent \
       sensor_mag_ak09940a

include ../common/modules/modules.mk

# Tag-local application and sensor sources. Local test.c shadows the shared
# test module through VPATH.
ALLCSRC += \
       config.c \
       datalog.c \
       lis2du12.c \
       pwr.c \
       sensors.c \
       state_run.c
