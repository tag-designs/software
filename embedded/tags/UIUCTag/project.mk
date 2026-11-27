# UIUCTag build manifest: the UIUCTag board with an ADXL362 accelerometer.
#
# UIUCadxl367 (../UIUCadxl367) builds these same sources for boards fitted
# with an ADXL367. The two differ only in the accelerometer module and the
# accel_*.c file below; keep anything else that changes here in step there.

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
       sensor_accel_adxl362 \
       debug_log

# Tag-local application sources. Shared BitPresTag family sources come from
# ../families/BitPresTag unless this directory provides a same-named file;
# sensors.c and accel_adxl362.c have no family counterpart, so they are
# listed here. accel_adxl367.c in ./src belongs to UIUCadxl367 and is not
# built for this target.
ALLCSRC += sensors.c accel_adxl362.c

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk
