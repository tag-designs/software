# BitTag build manifest.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagv6/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       sensor_accel_adxl362

include ../common/modules/modules.mk

# Activity-test-stand build: logs the RTC time and ADXL362 AWAKE level of every
# RUNNING wake into SRAM2 (inc/edge_log.h), and retains SRAM2 through Standby to
# keep it. Raises Standby current, so never use it for power measurements.
#   make BITTAG_EDGE_LOG=1 BUILDDIR=build-edgelog
ifeq ($(BITTAG_EDGE_LOG),1)
UDEFS += -DBITTAG_EDGE_LOG=1 -DTAG_RETAIN_SRAM2=1
endif

# BitTag keeps only its activity-log application code locally. Core runtime,
# monitor, persistence, power, RTC, and ADXL362 driver sources come from the
# common modules above.
ALLCSRC += \
       bt_config.c \
       bt_state_run.c \
       datalog.c \
       devices.c
