#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

// Stored Configuration

#define TAG_TYPE ACCELTAGNG

typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

typedef struct
{
  // adxl configuration
  uint16_t adxl_act_thresh_cnt;
  uint16_t adxl_inactive_samples;
  uint16_t adxl_active_samples;
  // data collection configuration
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  //bool internal;
  bool activity_monitor;
  uint8_t fifo_ctl_val;
  uint8_t fifo_samples;
  uint8_t filter_range_rate;
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

extern void writeStoredConfig(t_storedconfig *s);
extern bool writeConfig(Config *config);
extern void readConfig(Config *config);

#endif /* CONFIG_H */