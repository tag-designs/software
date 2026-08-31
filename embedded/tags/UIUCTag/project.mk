# UIUCTag build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/UIUCTag/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_pressure_bmp581 \
       sensor_accel_adxl367

# Tag-local application sources. Shared BitPresTag family sources come from
# ../families/BitPresTag unless this directory provides a same-named file;
# sensors.c has no family counterpart, so it is listed here.
ALLCSRC += sensors.c

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk
