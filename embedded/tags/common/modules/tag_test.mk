# Shared test/diagnostic entry points.
#
# Tags with a local src/test.c intentionally shadow ../common/test/src/test.c
# through the existing VPATH order.

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/test/src

ALLCSRC += \
       test.c
