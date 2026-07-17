# BMM350 magnetometer driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_mag_paths.mk

UDEFS += -DTAG_SENSOR_MAG_BMM350=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       bmm350_tag.c \
       bmm350_test.c
