# Shared storage source/header search paths.
#
# This helper is included by modules that need the external flash interface or
# implementations. It is guarded because multiple modules may need those paths.

ifndef TAG_STORAGE_PATHS_INCLUDED
TAG_STORAGE_PATHS_INCLUDED := yes

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/storage/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/storage/inc

endif
