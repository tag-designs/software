#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "tag.pb.h"

/* Stored Configuration */

#define TAG_TYPE BITTAG

typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

typedef struct
{
  uint16_t adxl_act_thresh_cnt;
  uint16_t adxl_inact_thresh_cnt;
  uint16_t adxl_inactive_samples;
  uint8_t  adxl_filter_range_rate;
  uint8_t  internal_format;
  uint8_t  fill1;
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  uint8_t fill[4];
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

void writeStoredConfig(t_storedconfig *s);
bool writeConfig(Config *config);
void readConfig(Config *config);

#endif /* CONFIG_H */
