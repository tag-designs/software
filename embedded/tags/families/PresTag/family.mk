# Shared application code for PresTag log-format variants.
#
# PresTag and PresTagRaw share board wiring, pressure sensor, storage, runtime
# state code, ChibiOS configuration, and the on-flash log layout. Variant
# project.mk files select the protobuf export format and keep their local
# custom.h files for firmware strings and build constants.

PRESTAG_FAMILY_DIR := ../families/PresTag

TAG_FAMILY_INC_DIRS += $(PRESTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(PRESTAG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(PRESTAG_FAMILY_DIR)/cfg

ALLCSRC += \
       $(PRESTAG_FAMILY_DIR)/src/config.c \
       $(PRESTAG_FAMILY_DIR)/src/datalog.c \
       $(PRESTAG_FAMILY_DIR)/src/devices.c \
       $(PRESTAG_FAMILY_DIR)/src/state_run.c
