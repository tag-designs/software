# Shared IMU source/header search paths.

ifndef TAG_SENSOR_IMU_PATHS_INCLUDED
TAG_SENSOR_IMU_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/imu
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/imu
MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/inc

endif
