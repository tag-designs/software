/**
 * @file    configfieldvisibility.cpp
 * @brief   Fills the configuration modules' visibility table from ConfigShape.
 *
 * @details Temporary adapter (stage 1 of the configuration shape and panels
 *          proposal). The modules still read ConfigFieldVisibility; the table
 *          now comes from the tag's ConfigShape instead of a build-time
 *          generator. Stage 1b replaces the modules, and this file goes with
 *          them.
 *
 * @see     docs/shared/design/proposals/config-shape-and-panels.md
 */

#include "configfieldvisibility.h"

#include "configshape.h"

ConfigFieldVisibility configFieldVisibilityForTag(TagType tag_type)
{
  const ConfigShape &s = ConfigShape::forTag(tag_type);
  ConfigFieldVisibility v;
  v.active_interval = s.has("active_interval");
  v.hibernate = s.has("hibernate");
  v.period = s.has("period");
  v.start_delay = s.has("start_delay");
  v.bittag_log = s.has("bittag_log");

  v.adxl362 = s.has("adxl362");
  v.adxl362_range = s.has("adxl362.range");
  v.adxl362_freq = s.has("adxl362.freq");
  v.adxl362_filter = s.has("adxl362.filter");
  v.adxl362_act_thresh_g = s.has("adxl362.act_thresh_g");
  v.adxl362_inact_thresh_g = s.has("adxl362.inact_thresh_g");
  v.adxl362_inactive_sec = s.has("adxl362.inactive_sec");
  v.adxl362_accel_type = s.has("adxl362.accel_type");
  v.adxl362_data_format = s.has("adxl362.data_format");
  v.adxl362_channels = s.has("adxl362.channels");
  v.adxl362_active_sec = s.has("adxl362.active_sec");

  v.lsm6 = s.has("lsm6");
  v.lsm6_odr = s.has("lsm6.odr");
  v.lsm6_accel_rng = s.has("lsm6.accel_rng");
  v.lsm6_gyro_rng = s.has("lsm6.gyro_rng");
  return v;
}
