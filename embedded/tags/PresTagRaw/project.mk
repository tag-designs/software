# PresTagRaw build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/PresTagv3/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_pressure_lps27

include ../common/modules/modules.mk

UDEFS += -DPRESTAG_RAW_LOG=1

include ../families/PresTag/family.mk

# Variant-specific application sources. Shared PresTag family sources come
# from ../families/PresTag unless this directory provides a local override.
