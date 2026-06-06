
#include <QWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>

#include <QDebug>
#include <QLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QAbstractButton>

#include "pbenumgroup.h"
#include "tag.pb.h"
#include "tagclass.h"
#include "lsm6dsvconfig.h"

static const std::vector<PBEnumGroup::binfo> accel_range_buttons{
    {Lsm6dsv_ACCEL_R2G, "+/- 2g", nullptr},
    {Lsm6dsv_ACCEL_R4G, "+/- 4g", nullptr},
    {Lsm6dsv_ACCEL_R8G, "+/- 8g", nullptr},
    {Lsm6dsv_ACCEL_R16G, "+/- 16g", nullptr}
};

static const std::vector<PBEnumGroup::binfo> gyro_range_buttons{
    {Lsm6dsv_GYRO_R125dps, "+/- 125dps", nullptr},
    {Lsm6dsv_GYRO_R250dps, "+/- 250dps", nullptr},
    {Lsm6dsv_GYRO_R500dps, "+/- 500dps", nullptr},
    {Lsm6dsv_GYRO_R1000dps, "+/- 1000dps", nullptr},
    {Lsm6dsv_GYRO_R2000dps, "+/- 2000dps", nullptr},
    {Lsm6dsv_GYRO_R4000dps, "+/- 4000dps", nullptr}
};

static const std::vector<PBEnumGroup::binfo> odr_buttons{
    {Lsm6dsv_ODR_S50, "50 Hz", nullptr},
    {Lsm6dsv_ODR_S100, "100 Hz", nullptr},
    {Lsm6dsv_ODR_S200, "200 Hz", nullptr},
    {Lsm6dsv_ODR_S400, "400 Hz", nullptr},
    {Lsm6dsv_ODR_S800, "800 Hz", nullptr},
    {Lsm6dsv_ODR_S1600, "1600 Hz", nullptr},
};

Lsm6dsvConfig::Lsm6dsvConfig(QWidget *parent) : QWidget(parent)
{
    vbox_ = new QVBoxLayout();
    setLayout(vbox_);

    configbox_ = new QGroupBox("IMU");
    QGridLayout *g_outer = new QGridLayout;

    odr_ = new PBEnumGroup("Sample Rate", odr_buttons);
    accel_range_ = new PBEnumGroup("Accelerometer Range", accel_range_buttons);
    gyro_range_ = new PBEnumGroup("Gyro Range", gyro_range_buttons);

    g_outer->addWidget(odr_, 0, 0);
    g_outer->addWidget(accel_range_, 0, 1);
    g_outer->addWidget(gyro_range_, 1, 1);

    configbox_->setLayout(g_outer);
    vbox_->addWidget(configbox_);
}

bool Lsm6dsvConfig::Attach(Tag &tag)
{
    return true;
}

Lsm6dsvConfig::~Lsm6dsvConfig(){}

bool Lsm6dsvConfig::GetConfig(Config &config){
    if (!active)
        return true;

    int id;
    Lsm6dsv lsm(config.lsm6());

    if (odr_)
    {
        id = odr_->checkedId();
        if (Lsm6dsv_ODR_IsValid(id))
            lsm.set_odr((Lsm6dsv_ODR) id);
    }

    if (accel_range_)
    {
        id = accel_range_->checkedId();
        if (Lsm6dsv_ACCEL_IsValid(id))
            lsm.set_accel_rng((Lsm6dsv_ACCEL) id);
    }

    if (gyro_range_)
    {
        id = gyro_range_->checkedId();
        if (Lsm6dsv_GYRO_IsValid(id))
            lsm.set_gyro_rng((Lsm6dsv_GYRO) id);
    }

    *config.mutable_lsm6() = lsm;
    return true;
}

bool Lsm6dsvConfig::SetConfig(const Config &config){
  if (!config.has_lsm6())
  {
    active = false;
    setVisible(false);
    return true;
  }
   
  Lsm6dsv lsm(config.lsm6());

  // initialize all widgets default visibility

  odr_->setVisible(true);
  gyro_range_->setVisible(true);
  accel_range_->setVisible(true);

  odr_->setCheckedId((int) lsm.odr());
  accel_range_->setCheckedId((int) lsm.accel_rng());
  gyro_range_->setCheckedId((int) lsm.gyro_rng());

  active = true;
  setVisible(true);
  return true;

}
