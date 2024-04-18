#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "string.h"
#include "ADXL367.h"

#define ADXL_RANGE(r) (((r) >> 6) & 3)
#define ADXL_RATE(r) ((r)&7)


// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  

/*
 * Write config in ram to flash
 */

void writeStoredConfig(t_storedconfig *s)
{
  uint32_t *src = (uint32_t *)s;
  uint32_t *dest = (uint32_t *)&sconfig;
  ssize_t size = sizeof(*s)/4;
  if (s)
  {
    chSysLock();
    FLASH_Unlock();
    FLASH_Program_Array(dest, src, size);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
  }
}

// Translation between Tag constants and ProtoBuf constants

static const Adxl362_Rng Adxl367RngToEnum[] = {
    [ADXL367_2G_RANGE] = Adxl362_R2G,
    [ADXL367_4G_RANGE] = Adxl362_R4G,
    [ADXL367_8G_RANGE] = Adxl362_R8G};

static const Adxl362_Odr Adxl367ODRToEnum[] = {
    [ADXL367_ODR_12P5HZ] = Adxl362_S12_5,
    [ADXL367_ODR_25HZ] = Adxl362_S25,
    [ADXL367_ODR_50HZ] = Adxl362_S50,
    [ADXL367_ODR_100HZ] = Adxl362_S100,
    [ADXL367_ODR_200HZ] = Adxl362_S200,
    [ADXL367_ODR_400HZ] = Adxl362_S400};

static const Adxl367Channel Adxl367ChannelToEnum[] = {
  [ADXL367_FIFO_FORMAT_XYZ] = AdxlChannel_xyz,
	[ADXL367_FIFO_FORMAT_X] = AdxlChannel_x,
	[ADXL367_FIFO_FORMAT_Y] = AdxlChannel_y,
	[ADXL367_FIFO_FORMAT_Z] = AdxlChannel_xyz,
  // other cases not supported
  [ADXL367_FIFO_FORMAT_ZA] = AdxlChannel_UNSPECIFIED
};

static const Adxl362_Adxl367DataFormat Adxl367FormatToEnum[] = {
  [ADXL367_8B]  = Adxl362_DF8,
  [ADXL367_12B] = Adxl362_DF12,
  [ADXL367_14B_CHID] = Adxl362_DF14,
  // other cases not supported
  [ADXL367_12B_CHID] = Adxl362_DF_UNSPECIFIED,
};

static int EnumToAdxl367Format(Adxl362_Adxl367DataFormat df){
  switch(df) {
    case Adxl362_DF8:
      return ADXL367_8B;
    case Adxl362_DF12:
      return ADXL367_12B;
    case Adxl362_DF14:
      return ADXL367_14B_CHID;
    default:
      return ADXL367_14B_CHID;
  }
};

static int EnumToAdxl367Channel(Adxl367Channel chn) {
  switch(chn) {
    case AdxlChannel_xyz:
      return ADXL367_FIFO_FORMAT_XYZ;
    case AdxlChannel_x:
      return ADXL367_FIFO_FORMAT_X;
    case AdxlChannel_y:
      return ADXL367_FIFO_FORMAT_Y;
    case AdxlChannel_z:
      return ADXL367_FIFO_FORMAT_Z;
    default:
      return  ADXL367_FIFO_FORMAT_XYZ;
  }
};

static int EnumToAdxl367Rng(Adxl362_Rng rng)
{
  switch (rng)
  {
  case Adxl362_R2G:
    return ADXL367_RANGE_2G;
  case Adxl362_R4G:
    return ADXL367_RANGE_4G;
  case Adxl362_R8G:
    return ADXL367_RANGE_8G;
  default:
    return -1;
  };
}

static int EnumToAdxl367ODR(Adxl362_Odr odr)
{
  switch (odr)
  {
  case Adxl362_S12_5:
    return ADXL367_ODR_12P5HZ;
  case Adxl362_S25:
    return ADXL367_ODR_25HZ;
  case Adxl362_S50:
    return ADXL367_ODR_50HZ;
  case Adxl362_S100:
    return ADXL367_ODR_100HZ;
  case Adxl362_S200:
    return ADXL367_ODR_200HZ;
  case Adxl362_S400:
    return ADXL367_ODR_400HZ;
  default:
    return -1;
  };
}

// See ADXL367 Data sheet
static const float Tdelta[] = {[ADXL367_ODR_12P5HZ] = 1 / 12.5,
                               [ADXL367_ODR_25HZ] = 1 / 25.0,
                               [ADXL367_ODR_50HZ] = 1 / 50.0,
                               [ADXL367_ODR_100HZ] = 1 / 100.0,
                               [ADXL367_ODR_200HZ] = 1 / 200.0,
                               [ADXL367_ODR_400HZ] = 1 / 400.0};

// See ADXL367 Data Sheet

static const float Sens[] = {[ADXL367_RANGE_2G] = 0.00025,
                             [ADXL367_RANGE_4G] = 0.0005,
                             [ADXL367_RANGE_8G] = 0.001};
/*
 * Read current config
 */

void readConfig(Config *config)
{

  // only called if not idle or test -- both of those return
  // predefined default config

  if (config == NULL)
    return;

  bzero(config, sizeof(*config));
  config->tag_type = TAG_TYPE;
  config->triaxis_activity = false;
  // Sensor configuration
  // convert from adxl values to configuration values
  // need stored values
  int range = ADXL_RANGE(ADXL367_RANGE_2G);
  int freq = ADXL_RATE(ADXL367_ODR_12P5HZ);

  config->has_adxl362 = true;
  config->adxl362.range = Adxl367RngToEnum[range];
  config->adxl362.freq = Adxl367ODRToEnum[freq];
  config->adxl362.accel_type = Adxl362_AdxlType_367;
  config->adxl362.data_format = 0; // place holder
  config->adxl362.channels = 0; // place holder
  config->adxl362.active_sec = 1.0; // place holder
  config->adxl362.inactive_sec = 1.0; // place holder
  config->adxl362.act_thresh_g = 0.5; // place holder
  config->adxl362.inact_thresh_g = 1.2; // place holder
  
  config->hibernate_count = 2; // number of hibernation messages

  for (int i = 0; i < 2; i++)
  {
    config->hibernate[i].start_epoch = sconfig.hibernate[i].start_epoch;
    config->hibernate[i].end_epoch = sconfig.hibernate[i].end_epoch;
  }
}

/*
 * Write config to ram
 */

bool writeConfig(Config *config)
{
   if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  int range = EnumToAdxl367Rng(Adxl362_R2G);
  int freq = EnumToAdxl367ODR(Adxl362_S12_5);


  if ((range < 0) || (freq < 0))
  {
    return false;
  }

  //config_tmp.adxl_filter_range_rate = (range << 6) | (1 << 5) | freq;
  int thresh = config->adxl362.act_thresh_g / Sens[range];
  thresh = (thresh > 0x1fff) ? 0x1fff : thresh;
  config_tmp.adxl_act_thresh_cnt = thresh;
  
  //thresh = config->adxl362.inact_thresh_g / Sens[range];
  //thresh = (thresh > 0x1fff) ? 0x1fff : thresh;
  //config_tmp.adxl_inact_thresh_cnt = thresh;

  int samples = config->adxl362.inactive_sec / Tdelta[freq];
  samples = samples > 255 ? 255 : samples;
  config_tmp.adxl_inactive_samples = samples;
  samples = config->adxl362.active_sec / Tdelta[freq];
  samples = samples > 65535 ? 65535 : samples;
  config_tmp.adxl_active_samples = samples;
  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
