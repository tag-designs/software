# UIUCadxl367 build manifest: the UIUCTag board with an ADXL367 accelerometer.
#
# Every source comes from ../UIUCTag; this directory holds only custom.h (the
# firmware string). The ADXL367 and ADXL362 are pin compatible, so the board,
# bus bindings, log format, stored configuration and host protocol are
# UIUCTag's. What differs is the accelerometer module, which defines
# TAG_SENSOR_ACCEL_ADXL367 (devices.h and config.c key on it), and the
# accel_*.c file that configures the part. Keep the rest in step with
# ../UIUCTag/project.mk.

# The monitor runs over the shared-memory mailbox, as on UIUCTag.
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

# UIUCTag's inc/ and src/ are searched after this directory's and before the
# BitPresTag family's, so UIUCTag's overrides of the family files (state_run.c,
# datalog.[ch], devices.[ch], config.c) apply here too, and ./inc/custom.h
# replaces UIUCTag's.
UIUCTAG_DIR := ../UIUCTag
TAG_FAMILY_INC_DIRS += $(UIUCTAG_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(UIUCTAG_DIR)/src

ALLCSRC += sensors.c accel_adxl367.c

include ../common/modules/modules.mk
include ../families/BitPresTag/family.mk
