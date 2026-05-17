# RV3028 real-time clock driver.

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/rtc/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/rtc/inc

ALLCSRC += \
       rtc_rv3028.c
