# TagUIUC build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/TagUIUC/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_pressure_bmp581 \
       sensor_accel_adxl367

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk