# Shared application code for BitPresTag flash variants.
#
# BitPresTag and BitPresTagMX25R share board wiring, sensor choices, runtime
# state code, logging, tests, and ChibiOS configuration.
# Variant project.mk files select the external flash module (AT25XE or MX25R)
# and keep their local custom.h files for firmware strings and build constants.

BITPRESTAG_FAMILY_DIR := ../families/BitPresTag

TAG_FAMILY_INC_DIRS += $(BITPRESTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(BITPRESTAG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(BITPRESTAG_FAMILY_DIR)/cfg

ALLCSRC += \
       $(BITPRESTAG_FAMILY_DIR)/src/config.c \
       $(BITPRESTAG_FAMILY_DIR)/src/datalog.c \
       $(BITPRESTAG_FAMILY_DIR)/src/devices.c \
       $(BITPRESTAG_FAMILY_DIR)/src/state_run.c
