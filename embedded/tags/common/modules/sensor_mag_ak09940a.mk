# AK09940A magnetometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_mag_paths.mk

UDEFS += -DTAG_SENSOR_MAG_AK09940A=1 -DUSE_AK09940A=1

ALLCSRC += \
       ak09940a.c
