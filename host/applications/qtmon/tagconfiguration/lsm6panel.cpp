/**
 * @file    lsm6panel.cpp
 * @brief   The LSM6DSV IMU panel.
 */

#include "lsm6panel.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>

#include "pbenumgroup.h"

static const std::vector<PBEnumGroup::binfo> accel_range_buttons{
    {Lsm6dsv_ACCEL_R2G, "+/- 2g", nullptr},
    {Lsm6dsv_ACCEL_R4G, "+/- 4g", nullptr},
    {Lsm6dsv_ACCEL_R8G, "+/- 8g", nullptr},
    {Lsm6dsv_ACCEL_R16G, "+/- 16g", nullptr}};

static const std::vector<PBEnumGroup::binfo> gyro_range_buttons{
    {Lsm6dsv_GYRO_R125dps, "+/- 125dps", nullptr},
    {Lsm6dsv_GYRO_R250dps, "+/- 250dps", nullptr},
    {Lsm6dsv_GYRO_R500dps, "+/- 500dps", nullptr},
    {Lsm6dsv_GYRO_R1000dps, "+/- 1000dps", nullptr},
    {Lsm6dsv_GYRO_R2000dps, "+/- 2000dps", nullptr},
    {Lsm6dsv_GYRO_R4000dps, "+/- 4000dps", nullptr}};

static const std::vector<PBEnumGroup::binfo> odr_buttons{
    {Lsm6dsv_ODR_S100, "100 Hz", nullptr},
    {Lsm6dsv_ODR_S200, "200 Hz", nullptr},
    {Lsm6dsv_ODR_S400, "400 Hz", nullptr},
    {Lsm6dsv_ODR_S800, "800 Hz", nullptr},
    {Lsm6dsv_ODR_S1600, "1600 Hz", nullptr}};

Lsm6Panel::Lsm6Panel(QWidget *parent) : ConfigPanel(parent)
{
  auto *vbox = new QVBoxLayout();
  setLayout(vbox);

  auto *configbox = new QGroupBox("IMU");
  auto *g_outer = new QGridLayout;

  odr_ = new PBEnumGroup("Sample Rate", odr_buttons);
  accel_range_ = new PBEnumGroup("Accelerometer Range", accel_range_buttons);
  gyro_range_ = new PBEnumGroup("Gyro Range", gyro_range_buttons);

  // One row of three, so the panel is short enough for a laptop screen.
  g_outer->addWidget(odr_, 0, 0);
  g_outer->addWidget(accel_range_, 0, 1);
  g_outer->addWidget(gyro_range_, 0, 2);

  configbox->setLayout(g_outer);
  vbox->addWidget(configbox);
}

std::vector<std::string_view> Lsm6Panel::paths() const
{
  return {"lsm6.odr", "lsm6.accel_rng", "lsm6.gyro_rng"};
}

bool Lsm6Panel::load(const Config &config)
{
  const Lsm6dsv &lsm = config.lsm6();
  odr_->setCheckedId((int)lsm.odr());
  accel_range_->setCheckedId((int)lsm.accel_rng());
  gyro_range_->setCheckedId((int)lsm.gyro_rng());
  return true;
}

bool Lsm6Panel::store(Config &config) const
{
  Lsm6dsv lsm(config.lsm6());
  int id = odr_->checkedId();
  if (Lsm6dsv_ODR_IsValid(id))
    lsm.set_odr((Lsm6dsv_ODR)id);
  id = accel_range_->checkedId();
  if (Lsm6dsv_ACCEL_IsValid(id))
    lsm.set_accel_rng((Lsm6dsv_ACCEL)id);
  id = gyro_range_->checkedId();
  if (Lsm6dsv_GYRO_IsValid(id))
    lsm.set_gyro_rng((Lsm6dsv_GYRO)id);
  *config.mutable_lsm6() = lsm;
  return true;
}
