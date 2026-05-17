# Shared light-sensor source/header search paths.

ifndef TAG_SENSOR_LIGHT_PATHS_INCLUDED
TAG_SENSOR_LIGHT_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/sensors/light/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/sensors/light/inc

endif
