# RV3028 real-time clock driver.
#
# hal_rtc_lld.c also lives in common/rtc/src. It is not listed in ALLCSRC
# because ChibiOS contributes that basename through the HAL source list; placing
# common/rtc/src ahead of the ChibiOS source directories in VPATH makes the tag
# build use the repo-local RTCv2 override.

MODULE_INC_DIRS += $(TAG_COMMON_DIR)/rtc/inc
UDEFS += -DTAG_RTC_RV3028=1

include $(TAG_COMMON_MODULE_DIR)/sensor_paths.mk

ifndef TAG_SENSOR_IO_SOURCE_INCLUDED
TAG_SENSOR_IO_SOURCE_INCLUDED := yes
ALLCSRC += sensor_io.c
endif

ifeq ($(USE_CHIBIOS_RTC_LLD),yes)
ALLCSRC += \
       $(TAG_COMMON_DIR)/rtc/src/rtc_device.c \
       $(TAG_COMMON_DIR)/rtc/src/rtc_rv3028.c \
       $(TAG_COMMON_DIR)/rtc/src/rtc_test.c
else
MODULE_SRC_DIRS += $(TAG_COMMON_DIR)/rtc/src
ALLCSRC += \
       rtc_device.c \
       rtc_rv3028.c \
       rtc_test.c
endif
