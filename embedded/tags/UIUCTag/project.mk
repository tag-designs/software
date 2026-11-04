# UIUCTag build manifest.

# The monitor runs over the shared-memory mailbox, not the DebugMonitor
# (common/core/src/handlersL4Mailbox.c). The mailbox linker script keeps
# MONITOR_SHARED_ADDR, the first 64 bytes of SRAM1, out of ram0.
UDEFS += -DTAG_L4_MONITOR_MAILBOX=1
LDSCRIPT = ../common/STM32L432xC-mailbox.ld

USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/UIUCTag/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_pressure_bmp581 \
       sensor_accel_adxl367 \
       debug_log

# Tag-local application sources. Shared BitPresTag family sources come from
# ../families/BitPresTag unless this directory provides a same-named file;
# sensors.c has no family counterpart, so it is listed here.
ALLCSRC += sensors.c

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk
