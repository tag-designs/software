# Shared application code for IMUTag processor and board variants.
#
# Variant project.mk files include this manifest after selecting their board and
# tag modules. The common makefile keeps local cfg/, inc/, and src/ before
# these family directories, so a variant can temporarily override a shared file
# by placing a same-named file in its local tree.

IMUTAG_FAMILY_DIR := ../families/IMUTag

TAG_FAMILY_INC_DIRS += $(IMUTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(IMUTAG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(IMUTAG_FAMILY_DIR)/cfg

ALLCSRC += \
       config.c \
       datalog.c \
       devices.c \
       sensors.c \
       state_run.c \
       stubs.c
