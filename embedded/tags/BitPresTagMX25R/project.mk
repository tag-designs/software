# BitPresTagMX25R build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitPresTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_mx25r \
       sensor_pressure_lps27 \
       sensor_accel_adxl362

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk

# Shared BitPresTag family sources come from ../families/BitPresTag. This
# variant differs from BitPresTag only by selecting flash_mx25r above.
