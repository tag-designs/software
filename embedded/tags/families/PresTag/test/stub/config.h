#ifndef STUB_CONFIG_H
#define STUB_CONFIG_H
#include <stdbool.h>
#include <stdint.h>
#include "tag.pb.h"
typedef struct {
  int32_t start, stop;
  Config_Interval hibernate[2];
  uint32_t lps_period;
  bool lps_low_noise;
} t_storedconfig;
extern t_storedconfig sconfig;
#endif
