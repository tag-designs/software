#ifndef APP_H
#define APP_H

// Compatibility umbrella for tag firmware. New code should prefer including
// the narrower topic headers below.
#include "ch.h"
#include "hal.h"
#include "monitor.h"
#include "custom.h"
#include "mcuconf.h"
#include "tag.pb.h"

#include "adc.h"
#include "core_events.h"
#include "core_runtime.h"
#include "core_state.h"
#include "core_sync.h"
#include "core_types.h"
#include "debug_log.h"
#include "flash_internal.h"
#include "gpio_utils.h"
#include "power.h"
#include "rtc_api.h"
#include "sensor_calibration.h"
#include "test_support.h"
#include "timekeeping.h"

#endif
