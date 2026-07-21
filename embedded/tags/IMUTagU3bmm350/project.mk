# IMUTagU3bmm350 build manifest.
USE_HAL_I2C_FALLBACK = yes
USE_CHIBIOS_RTC_LLD = yes
USE_PROCESS_STACKSIZE = 0x800
USE_EXCEPTIONS_STACKSIZE = 0x800
TAG_FLASH_SIZE = 1024K
UDEFS += -DTAG_STM32U3_FLASH=1
UDEFS += -DIMUTAG_STM32U3_FLASH=1
UDEFS += -DIMUTAG_STORED_CONFIG_STM32U3_FLASH=1
UDEFS += -DTAG_RTC_I2C_DELAY_CYCLES=64U
UDEFS += -DTAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN=1
include $(BOARDDIR)/IMUTagU375/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       debug_log \
       tag_test \
       rtc_rv3028 \
       flash_mx25u12843 \
       sensor_pressure_lps22hh \
       sensor_mag_bmm350 \
       sensor_imu_lsm6dsv16x

include ../common/modules/modules.mk
include ../families/IMUTag/family.mk

# Variant-specific application sources. Shared IMUTag family sources come from
# ../families/IMUTag unless this directory provides a local source with the
# same name.
