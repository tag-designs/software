# BitTag build manifest.
#USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       sensor_accel_adxl362 \
       bittag_runtime

include ../common/modules/modules.mk
