# IMUTagNand build manifest.
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
include $(BOARDDIR)/IMUTagNandv1/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_gd5f1gq5re \
       sensor_pressure_lps22hh \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x

ALLCSRC += power_modes.c

include ../common/modules/modules.mk
include ../families/IMUTag/family.mk

# Variant-specific application sources. Shared IMUTag family sources come from
# ../families/IMUTag unless this directory provides a local source with the
# same name.
