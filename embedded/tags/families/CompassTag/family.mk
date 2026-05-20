# Shared application code for CompassTag production and breakout builds.
#
# Variant project.mk files include this manifest after selecting their board and
# tag modules.  The common makefile keeps local cfg/, inc/, and src/ before
# these family directories, so a variant can temporarily override a shared file
# by placing a same-named file in its local tree.

COMPASSTAG_FAMILY_DIR := ../families/CompassTag

TAG_FAMILY_INC_DIRS += $(COMPASSTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(COMPASSTAG_FAMILY_DIR)/src
TAG_FAMILY_CFG_DIRS += $(COMPASSTAG_FAMILY_DIR)/cfg

UDEFS += -DTAG_SENSOR_ACCEL_LIS2DU12=1

ALLCSRC += \
       config.c \
       devices.c \
       lis2du12.c \
       lis2du12_test.c \
       $(COMPASSTAG_FAMILY_DIR)/src/datalog.c \
       $(COMPASSTAG_FAMILY_DIR)/src/sensors.c \
       $(COMPASSTAG_FAMILY_DIR)/src/state_run.c
