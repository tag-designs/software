#ifndef CONFIG_H
#define CONFIG_H

// Stored Configuration

#define TAG_TYPE BITPRESTAG

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
  uint8_t  fill1;
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  //bool internal;
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

extern void writeStoredConfig(t_storedconfig *s);
extern bool writeConfig(Config *config);
extern void readConfig(Config *config);

#endif /* CONFIG_H */