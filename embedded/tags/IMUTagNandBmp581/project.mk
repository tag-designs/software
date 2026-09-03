# IMUTagNandBmp581 build manifest.
#
# This target keeps the shared IMUTag protocol identity while selecting the
# replacement BMP581/GD5F2GM7RE hardware stack. Keep this module list aligned
# with README.md and embedded/tags/BUILD_SOURCES.md whenever sources move.
USE_CHIBIOS_RTC_LLD = yes
USE_PROCESS_STACKSIZE = 0x800
USE_EXCEPTIONS_STACKSIZE = 0x800
TAG_FLASH_SIZE = 1024K
UDEFS += -DTAG_STM32U3_FLASH=1
UDEFS += -DIMUTAG_STM32U3_FLASH=1

# Recover an I2C bus that a slave is holding. The RV-3028 and the magnetometer
# share one controller here, and a monitor attach resets the core under an
# in-flight transaction, which left both devices unreachable for the rest of
# that boot: SDA low, SCL high, and every read failing. Set as a compile
# definition rather than in custom.h because i2c_bus.h applies its own default
# and does not include custom.h, so a header define would depend on include
# order.
UDEFS += -DTAG_I2C_BUS_CLEAR=1
UDEFS += -DIMUTAG_STORED_CONFIG_STM32U3_FLASH=1
include $(BOARDDIR)/IMUTagNandv2/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_gd5f2gm7re \
       sensor_pressure_bmp581 \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x

# WARNING: enabling debug_log breaks Stop3 entry on STM32U375. A tag built with
# it reports IDLE but never reaches standby, drawing about 1.7 mA instead of
# 6.6 uA -- reproduced on hardware, and it survives tag-reset. The cause is in
# the module itself and is unfixed; do not enable it on a U375 target, and it
# does not belong in shipped code regardless.
#
# Note the entry below is also orphaned: sensor_imu_lsm6dsv16x carries no
# trailing backslash, so simply uncommenting this line would not add the module
# to TAG_MODULES anyway.
       # debug_log \

ALLCSRC += power_modes.c

include ../common/modules/modules.mk
include ../families/IMUTag/family.mk

# Variant-specific application sources. Shared IMUTag family sources come from
# ../families/IMUTag unless this directory provides a local source with the
# same name.
