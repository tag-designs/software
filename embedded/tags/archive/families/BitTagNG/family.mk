# Shared application code for BitTagNG build variants.
#
# This family captures the configuration and files that are identical between
# the ADXL367 and LIS2DU12 variants.  The accelerometer driver, runtime state
# code, tests, and custom tag constants remain variant-local.

BITTAGNG_FAMILY_DIR := ../families/BitTagNG

TAG_FAMILY_INC_DIRS += $(BITTAGNG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(BITTAGNG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(BITTAGNG_FAMILY_DIR)/cfg

ALLCSRC += \
       config.c
