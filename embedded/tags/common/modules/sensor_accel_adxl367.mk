# ADXL367 accelerometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_accel_paths.mk

UDEFS += -DTAG_SENSOR_ACCEL_ADXL367=1

ALLCSRC += \
       ADXL367.c \
       adxl367_test.c
