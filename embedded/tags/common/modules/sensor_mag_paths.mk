# Shared magnetometer source/header search paths.

ifndef TAG_SENSOR_MAG_PATHS_INCLUDED
TAG_SENSOR_MAG_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/mag/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/mag/inc

endif
