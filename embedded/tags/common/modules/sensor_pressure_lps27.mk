# LPS27 pressure sensor driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk

UDEFS += -DTAG_SENSOR_PRESSURE_LPS27=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       pressure_device.c \
       lps27.c \
       lps27_test.c
