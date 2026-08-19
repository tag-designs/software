# IMUTagbmm350 build manifest.
USE_HAL_I2C_FALLBACK = yes
USE_PROCESS_STACKSIZE = 0x800
USE_EXCEPTIONS_STACKSIZE = 0x800
include $(BOARDDIR)/IMUTagv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       debug_log \
       tag_test \
       rtc_rv3028 \
       flash_mx25l \
       sensor_pressure_lps22hh \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x

include ../common/modules/modules.mk
include ../families/IMUTag/family.mk
