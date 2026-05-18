# LPS22HH pressure sensor driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk

UDEFS += -DTAG_SENSOR_PRESSURE_LPS22HH=1

ALLCSRC += \
       sensor_io.c \
       lps22hh.c
