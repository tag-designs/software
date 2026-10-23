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
 * @note    A value the panel has no button for, including `UNSPECIFIED`,
 *          leaves the group showing its first button (or the button a
 *          previous load() checked), and store() then writes that button's
 *          value. For example, an unknown accelerometer range is sent as 2 g.
 */
class Lsm6Panel : public ConfigPanel
{
public:
  /**
   * @brief Builds the sample rate and range button groups, in one row.
   * @param[in] parent  Qt parent.
   */
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
