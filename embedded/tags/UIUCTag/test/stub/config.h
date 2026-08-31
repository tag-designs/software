#ifndef STUB_CONFIG_H
#define STUB_CONFIG_H
#include <stdint.h>
#include "tag.pb.h"
typedef struct {
  uint16_t adxl_act_thresh_cnt, adxl_inact_thresh_cnt, adxl_inactive_samples;
  uint8_t adxl_filter_range_rate, fill1;
  int32_t start, stop;
  Config_Interval hibernate[2];
} t_storedconfig;
extern t_storedconfig sconfig;
#endif
