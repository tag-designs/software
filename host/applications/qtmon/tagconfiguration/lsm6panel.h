/**
 * @file    lsm6panel.h
 * @brief   Configuration panel for the LSM6DSV IMU (IMUTag).
 */

#ifndef LSM6PANEL_H
#define LSM6PANEL_H

#include "configpanel.h"

class PBEnumGroup;

/**
 * @class   Lsm6Panel
 * @brief   Edits `Config.lsm6`: sample rate, accelerometer range and gyro range.
 *
 * @details store() leaves a field unchanged when its control holds no valid
 *          choice, which happens only when the tag reported a value the panel
 *          has no button for.
 */
class Lsm6Panel : public ConfigPanel
{
public:
  explicit Lsm6Panel(QWidget *parent = nullptr);

  std::vector<std::string_view> paths() const override;
  bool load(const Config &config) override;
  bool store(Config &config) const override;

private:
  PBEnumGroup *odr_ = nullptr;
  PBEnumGroup *accel_range_ = nullptr;
  PBEnumGroup *gyro_range_ = nullptr;
};

#endif // LSM6PANEL_H
