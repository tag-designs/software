# BMP581 pressure sensor driver.
#
# Selects the descriptor-backed Bosch BMP5 SensorAPI wrapper. The wrapper keeps
# the legacy IMUTag pressure hooks and RUN_LPS self-test name while talking to
# the BMP581 on the board-provided pressure SPI bus.

include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk

UDEFS += -DTAG_SENSOR_PRESSURE_BMP581=1
MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/pressure/vendor/bmp5
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/pressure/vendor/bmp5

# sensor_io.c is shared by pressure, IMU, and magnetometer modules, so include
# it once even when multiple sensor modules request register-bus helpers.
ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

# pressure_device.c owns the generic pressure bus lifecycle. bmp581.c adapts it
# to Bosch callbacks, bmp581_test.c provides the historical RUN_LPS binding, and
# bmp5.c is the vendored Bosch implementation.
ALLCSRC += \
       pressure_device.c \
       bmp581.c \
       bmp581_test.c \
       bmp5.c
