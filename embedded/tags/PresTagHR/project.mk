# PresTagHR build manifest.
#
# PresTag on the PresTagv3 board, logging the LPS27's full 24-bit pressure:
# 48 five-byte samples per 240-byte page, exported as PresTagHRLog. Low-noise
# conversion is a stored-configuration option (Config.lps27.low_noise).
# See ../families/PresTag/design/proposals/prestaghr.md.
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

UDEFS += -DPRESTAG_HR_LOG=1

include ../families/PresTag/family.mk

# Variant-specific application sources. Shared PresTag family sources come
# from ../families/PresTag unless this directory provides a local override.
