#ifndef CONFIG_H
#define CONFIG_H

// Stored Configuration

#define TAG_TYPE PRESTAG

typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

typedef struct
{
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  uint32_t lps_period;
  bool internal;
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

extern void writeStoredConfig(t_storedconfig *s);
extern bool writeConfig(Config *config);
extern void readConfig(Config *config);

#endif /* CONFIG_H */