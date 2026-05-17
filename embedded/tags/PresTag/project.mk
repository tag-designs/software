# PresTag build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/PresTagv3/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       monitor \
       rtc_rv3028 \
       flash_at25xe \
       storage_persistent \
       sensor_pressure_lps27

include ../common/modules/modules.mk

# Tag-local application sources.
ALLCSRC += \
       config.c \
       datalog.c \
       pwr.c \
       state_run.c
