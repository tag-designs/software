# Shared application code for BitPresTag flash variants.
#
# BitPresTag and BitPresTagMX25R share board wiring, sensor choices, runtime
# state code, logging, tests, and power/bus control.  Variant project.mk files
# select the external flash module (AT25XE or MX25R) and keep their local
# custom.h/cfg files for firmware strings and build constants.

BITPRESTAG_FAMILY_DIR := ../families/BitPresTag

TAG_FAMILY_INC_DIRS += $(BITPRESTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(BITPRESTAG_FAMILY_DIR)/src

ALLCSRC := $(filter-out pwr.c test.c,$(ALLCSRC))

ALLCSRC += \
       $(BITPRESTAG_FAMILY_DIR)/src/config.c \
       $(BITPRESTAG_FAMILY_DIR)/src/datalog.c \
       $(BITPRESTAG_FAMILY_DIR)/src/pwr.c \
       $(BITPRESTAG_FAMILY_DIR)/src/state_run.c \
       $(BITPRESTAG_FAMILY_DIR)/src/test.c
