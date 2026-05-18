# Shared accelerometer source/header search paths.

ifndef TAG_SENSOR_ACCEL_PATHS_INCLUDED
TAG_SENSOR_ACCEL_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/accel/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/accel/inc
MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/inc

endif
