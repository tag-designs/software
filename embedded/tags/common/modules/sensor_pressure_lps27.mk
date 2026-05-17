# LPS27 pressure sensor driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk

UDEFS += -DTAG_SENSOR_PRESSURE_LPS27=1 -DUSE_LPS27=1

ALLCSRC += \
       lps27.c
