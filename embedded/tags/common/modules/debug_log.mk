# Optional in-firmware debug log exposed through the monitor protocol.
#
# Selecting this module owns the RAM buffer, ChibiOS stream support, and the
# compile-time switch used by core/driver code. DEBUG_MESSAGES is retained as a
# temporary compatibility alias for older source that has not yet been renamed.

include $(CHIBIOS)/os/hal/lib/streams/streams.mk

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/core/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/core/inc

UDEFS += -DTAG_DEBUG_LOG=1 -DDEBUG_MESSAGES=1

ALLCSRC += \
       debug_log.c
