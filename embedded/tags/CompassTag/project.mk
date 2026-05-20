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
include ../families/CompassTag/family.mk

# Variant-specific application sources. Shared CompassTag family sources come
# from ../families/CompassTag unless this directory provides a local source with
# the same name.
