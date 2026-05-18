# AK09940A magnetometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_mag_paths.mk

UDEFS += -DTAG_SENSOR_MAG_AK09940A=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       ak09940a.c \
       ak09940a_test.c
