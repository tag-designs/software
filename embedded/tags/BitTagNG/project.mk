# List of all the project related files.
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/BitTagNG/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_accel_adxl367

include ../common/modules/modules.mk
include ../families/BitTagNG/family.mk

ALLCSRC += \
       datalog.c \
       state_run.c
   
