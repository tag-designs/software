# Shared test/diagnostic entry points.
#
# Tags with a local src/test.c intentionally shadow ../common/src/test.c
# through the existing VPATH order.

include $(TAG_COMMON_MODULE_DIR)/sensor_paths.mk

ALLCSRC += \
       test.c
