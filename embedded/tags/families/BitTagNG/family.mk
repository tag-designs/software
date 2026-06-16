# Shared application code for BitTagNG build variants.
#
# This family captures the configuration and board-facing device descriptors
# shared by BitTagNG build variants.

BITTAGNG_FAMILY_DIR := ../families/BitTagNG

TAG_FAMILY_INC_DIRS += $(BITTAGNG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(BITTAGNG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(BITTAGNG_FAMILY_DIR)/cfg

ALLCSRC += \
       config.c \
       devices.c \
       sensors.c
