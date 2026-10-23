#ifndef CONFIG_FIELD_VISIBILITY_H
#define CONFIG_FIELD_VISIBILITY_H

#include "tag.pb.h"

struct ConfigFieldVisibility
{
  bool active_interval = false;
  bool hibernate = false;
  bool period = false;
  bool start_delay = false;
  bool bittag_log = false;

  bool adxl362 = false;
  bool adxl362_range = false;
  bool adxl362_freq = false;
  bool adxl362_filter = false;
  bool adxl362_act_thresh_g = false;
  bool adxl362_inact_thresh_g = false;
  bool adxl362_inactive_sec = false;
  bool adxl362_accel_type = false;
  bool adxl362_data_format = false;
  bool adxl362_channels = false;
  bool adxl362_active_sec = false;

  bool lsm6 = false;
  bool lsm6_odr = false;
  bool lsm6_accel_rng = false;
  bool lsm6_gyro_rng = false;
};

/**
 * @brief   Which configuration controls apply to a tag type.
 *
 * @param[in] tag_type  Attached tag's type.
 * @return  One flag per control group and field, read from the tag type's
 *          ConfigShape (its default configuration); all false for a tag type
 *          with no default configuration.
 */
ConfigFieldVisibility configFieldVisibilityForTag(TagType tag_type);

#endif // CONFIG_FIELD_VISIBILITY_H
