# LIS2DU12 accelerometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_accel_paths.mk

UDEFS += -DTAG_SENSOR_ACCEL_LIS2DU12=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       lis2du12.c \
       lis2du12_test.c
