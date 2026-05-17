# Include shared tag build modules listed in TAG_MODULES.
#
# This file is included from each tag's project.mk, where the current working
# directory is the tag directory. Keep the module path relative to that layout.

TAG_COMMON_MODULE_DIR ?= ../common/modules

include $(foreach module,$(TAG_MODULES),$(TAG_COMMON_MODULE_DIR)/$(module).mk)

