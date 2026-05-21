# Shared source/header search paths for every common sensor family.
#
# Most production modules add only the sensor family they compile, but shared
# core headers and tag/family device tables may reference test hooks or device
# descriptors from several sensor families. This guarded helper gives that code
# access to the common sensor headers while preserving tag-local include/source
# override priority from ../make.mk.

ifndef TAG_SENSOR_PATHS_INCLUDED
TAG_SENSOR_PATHS_INCLUDED := yes

include $(TAG_COMMON_MODULE_DIR)/sensor_accel_paths.mk
include $(TAG_COMMON_MODULE_DIR)/sensor_pressure_paths.mk
include $(TAG_COMMON_MODULE_DIR)/sensor_mag_paths.mk
include $(TAG_COMMON_MODULE_DIR)/sensor_light_paths.mk

endif
