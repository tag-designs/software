# LPS27 pressure sensor driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk

UDEFS += -DTAG_SENSOR_PRESSURE_LPS27=1

ALLCSRC += \
       sensor_io.c \
       lps.c \
       lps27.c \
       lps27_test.c
