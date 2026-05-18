# ADXL362 accelerometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_accel_paths.mk

UDEFS += -DTAG_SENSOR_ACCEL_ADXL362=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       ADXL362.c \
       adxl362_test.c
