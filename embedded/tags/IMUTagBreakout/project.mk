# IMUTagBreakout build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/IMUTagv1/board.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       monitor \
       rtc_rv3028 \
       flash_mx25l \
       storage_persistent

include ../common/modules/modules.mk

# Tag-local application and sensor sources. Local test.c shadows the shared
# test module through VPATH.
ALLCSRC += \
       ak09940.c \
       config.c \
       datalog.c \
       lps22hh.c \
       pwr.c \
       sensors.c \
       state_run.c \
       stubs.c      
