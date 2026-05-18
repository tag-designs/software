# Shared pressure-sensor source/header search paths.

ifndef TAG_SENSOR_PRESSURE_PATHS_INCLUDED
TAG_SENSOR_PRESSURE_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/pressure/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/pressure/inc
MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/inc

endif
