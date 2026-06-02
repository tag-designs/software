# LSM6DSV16X IMU driver.

include $(TAG_COMMON_MODULE_DIR)/sensor_imu_paths.mk

UDEFS += -DTAG_SENSOR_IMU_LSM6DSV16X=1

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ALLCSRC += \
       lsm6dsv16x.c \
       lsm6dsv16x_test.c
