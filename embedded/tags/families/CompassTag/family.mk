# Shared application code for CompassTag production and breakout builds.
#
# Variant project.mk files include this manifest after selecting their board and
# tag modules.  The common makefile keeps ./inc and ./src before these family
# directories, so a variant can temporarily override a shared file by placing a
# same-named file in its local inc/ or src/ directory.

COMPASSTAG_FAMILY_DIR := ../families/CompassTag

TAG_FAMILY_INC_DIRS += $(COMPASSTAG_FAMILY_DIR)/inc
TAG_FAMILY_SRC_DIRS += $(COMPASSTAG_FAMILY_DIR)/src

UDEFS += -DTAG_SENSOR_ACCEL_LIS2DU12=1

ALLCSRC := $(filter-out pwr.c,$(ALLCSRC))

ALLCSRC += \
       config.c \
       lis2du12.c \
       lis2du12_test.c \
       $(COMPASSTAG_FAMILY_DIR)/src/pwr.c \
       $(COMPASSTAG_FAMILY_DIR)/src/sensors.c
