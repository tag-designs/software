# Core tag runtime shared by most tag firmware targets.

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/core/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/core/inc

ALLCSRC += \
       handlers.c \
       main.c \
       monitor.c \
       state_machine.c \
       stm32adc.c \
       stm32flash.c \
       time.c
