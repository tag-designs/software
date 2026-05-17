# RV3028 real-time clock driver.
#
# hal_rtc_lld.c also lives in common/rtc/src. It is not listed in ALLCSRC
# because ChibiOS contributes that basename through the HAL source list; placing
# common/rtc/src ahead of the ChibiOS source directories in VPATH makes the tag
# build use the repo-local RTCv2 override.

MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/rtc/src
MODULE_INC_DIRS += $(TAG_COMMON_DIR)/rtc/inc
UDEFS += -DTAG_RTC_RV3028=1

ALLCSRC += \
       rtc_rv3028.c \
       rtc_test.c
