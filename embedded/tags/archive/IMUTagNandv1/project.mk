# IMUTagNandv1 build manifest.
USE_PROCESS_STACKSIZE = 0x800
USE_EXCEPTIONS_STACKSIZE = 0x800
include $(BOARDDIR)/IMUTagNandv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       debug_log \
       tag_test \
       rtc_rv3028 \
       flash_gd5f1gq5re \
       sensor_pressure_lps22hh \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x

include ../common/modules/modules.mk
include ../families/IMUTag/family.mk

# Variant-specific application sources. Shared IMUTag family sources come from
# ../families/IMUTag unless this directory provides a local source with the
# same name.
