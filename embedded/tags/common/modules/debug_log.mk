# Optional in-firmware debug log exposed through the monitor protocol.
#
# WARNING: this module breaks Stop3 low-power entry on STM32U375. A tag built
# with it reports IDLE but never enters standby, drawing about 1.7 mA rather
# than 6.6 uA, and the condition survives a tag-reset. Reproduced on hardware
# with the persistent flash region pinned, so it is not the older layout
# problem where the module's ~9.6 kB of extra code relocated .persistent. The
# defect is in this module and is unfixed.
#
# Do not enable on a U375 target. It is a bench diagnostic and does not belong
# in shipped firmware in any case: prefer the state marker log, readable with
# tag-info, and the retained run diagnostics in pState.
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
