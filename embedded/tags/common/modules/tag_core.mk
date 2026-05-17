# Core tag runtime shared by most tag firmware targets.

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/core/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/core/inc

include $(TAG_COMMON_MODULE_DIR)/storage_paths.mk
include $(TAG_COMMON_MODULE_DIR)/sensor_paths.mk

ALLCSRC += \
       bus_power.c \
       handlers.c \
       main.c \
       monitor.c \
       persistent.c \
       pwr.c \
       state_machine.c \
       stm32adc.c \
       stm32flash.c \
       time.c
