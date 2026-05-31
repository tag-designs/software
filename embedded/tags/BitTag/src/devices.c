#include "hal.h"

#include "core_sync.h"
#include "custom.h"
#include "device.h"
#include "devices.h"
#include "power.h"
#include "test_support.h"

#ifndef ACCEL_WAKEUP_SOURCE
#define ACCEL_WAKEUP_SOURCE 4
#endif

#if ACCEL_WAKEUP_SOURCE == 1
#define TAG_ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP1
#define TAG_ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP1_Msk
#elif ACCEL_WAKEUP_SOURCE == 4
#define TAG_ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP4
#define TAG_ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP4_Msk
#else
#error "Unsupported ACCEL_WAKEUP_SOURCE"
#endif

binary_semaphore_t SPI1mutex;

void tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
}

const TagAdxl362Device tagBitTagAccelDevice = {
    .bus = TAG_BUS_SPI_INIT(
        TAG_SPI1_DEVICE_DEFAULTS,
        .cs = LINE_ACCEL_CS,
        .sck = LINE_ACCEL_SCK,
        .miso = LINE_ACCEL_MISO,
        .mosi = LINE_ACCEL_MOSI,
        .pwr = TAG_NO_LINE,
        .dummy = 0xff,
        .sleep_policy = TAG_SPI_SLEEP_FLOAT),
};

static const TagTestCase tag_tests[] =
{
  {RUN_ADXL362, tag_test_adxl362, TAG_ACCEL_DEVICE},
  {RUN_RTC, tag_test_rtc, NULL},
};

const TagTestCase *tagTestCases(size_t *count)
{
  *count = sizeof(tag_tests) / sizeof(tag_tests[0]);
  return tag_tests;
}

const TagAdxl362Device *tagAdxl362Device(void)
{
  return TAG_ACCEL_DEVICE;
}

void tagDevicesApplyStandbyPins(void)
{
  /* Legacy static-board fallback; generated BitTagv6 uses board_standby.h. */
  tagEnableStandbyPullup(LINE_ACCEL_CS);
}

void tagDevicesDisableWakeupSources(void)
{
  CLEAR_BIT(PWR->CR3, TAG_ACCEL_WAKEUP_ENABLE_BIT);
}

bool tagDevicesConfigureWakeupSources(uint32_t state, bool is_active)
{
  if (is_active)
    SET_BIT(PWR->CR4, TAG_ACCEL_WAKEUP_POLARITY_BIT);
  else
    CLEAR_BIT(PWR->CR4, TAG_ACCEL_WAKEUP_POLARITY_BIT);

  if (state == RUNNING)
  {
    SET_BIT(PWR->CR3, TAG_ACCEL_WAKEUP_ENABLE_BIT | PWR_CR3_EIWF_Msk);
    return is_active == palReadLine(LINE_ACCEL_INT);
  }

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  return true;
}

void tagDevicesDeinit(void)
{
  ADXL362_DeinitDevice(TAG_ACCEL_DEVICE);
  chThdSleepMilliseconds(2);
}
