
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
#include "adxl362config.h"

static const std::vector<PBEnumGroup::binfo> adxl_data_size_buttons {
    {Adxl362_Adxl367DataFormat_DF14, "14-bit data", nullptr},
    {Adxl362_Adxl367DataFormat_DF12,"12-bit data", nullptr},
    {Adxl362_Adxl367DataFormat_DF8, "8-bit data",nullptr}
};

static const std::vector<PBEnumGroup::binfo> adxl_channels_buttons {
  {AdxlChannel_xyz,"X,Y,Z Channels", nullptr},
  {AdxlChannel_x,"X Channel", nullptr},
  {AdxlChannel_y,"Y Channel", nullptr},
  {AdxlChannel_z,"Z Channel", nullptr} /*,
  {AdxlChannel_xyzt,"X,Y,Z,T Channels", nullptr},
  {AdxlChannel_xt,"X,T Channels", nullptr},
  {AdxlChannel_yt,"Y,T Channels", nullptr},
  {AdxlChannel_zt,"Z,T Channels", nullptr},
  {AdxlChannel_xyze,"X,Y,Z,E Channels", nullptr},
  {AdxlChannel_xe,"X,E Channels", nullptr},
  {AdxlChannel_ye,"Y,E Channels", nullptr},
  {AdxlChannel_ze,"Z,E Channels", nullptr}*/
};

static const std::vector<PBEnumGroup::binfo> adxl_range_buttons{
    {Adxl362_Rng_R2G, "+/- 2g", nullptr},
    {Adxl362_Rng_R4G, "+/- 4g", nullptr},
    {Adxl362_Rng_R8G, "+/- 8g", nullptr}};

static const std::vector<PBEnumGroup::binfo> adxl_odr_buttons{
    {Adxl362_Odr_S12_5, "12.5 Hz", "12.5 samples/second"},
    {Adxl362_Odr_S25, "25 Hz", "25 samples/second"},
    {Adxl362_Odr_S50, "50 Hz", "50 samples/second"},
    {Adxl362_Odr_S100, "100 Hz", "100 samples/second"},
    {Adxl362_Odr_S200, "200 Hz", "200 samples/second"},
    {Adxl362_Odr_S400, "400 Hz", "400 samples/second"}};

static const std::vector<PBEnumGroup::binfo> adxl_filter_buttons{
    {Adxl362_Aa_AAquarter, "1/4 Sample Rate", "filter cutoff -- 1/4 sample rate"},
    {Adxl362_Aa_AAhalf, "1/2 Sample Rate", "filter cutoff -- 1/2 sample rate"}};

Adxl362Config::Adxl362Config(QWidget *parent) : QWidget(parent)
{
  vbox_ = new QVBoxLayout();
  setLayout(vbox_);

  // Build the interface

  configbox_ = new QGroupBox("Accelerometer");
  QGridLayout *g_outer = new QGridLayout;
  QFormLayout *f_lower = new QFormLayout;

  sample_rate_ = new PBEnumGroup("Sample Rate", adxl_odr_buttons);
  range_ = new PBEnumGroup("Range", adxl_range_buttons);
  filter_ = new PBEnumGroup("Filter", adxl_filter_buttons);
  data_size_ = new PBEnumGroup("Data Size", adxl_data_size_buttons);
  channels_ = new PBEnumGroup("Channels", adxl_channels_buttons);

  spinners_ = new QGroupBox("Activity Detection");

  sample_rate_->setToolTip("Set Accelerometer Sample Rate");
  range_->setToolTip("Set Accelerometer Range");
  filter_->setToolTip("Set Accelerometer Anti-Alias Filter");
  spinners_->setToolTip("Set Activity Detection Parameters");
  channels_->setToolTip("Active Accelerometer Data Channels");
  data_size_->setToolTip("Accelerometer Data Sample Bits");
  // range and filter

  g_outer->addWidget(sample_rate_, 1, 0, 2, 1);
  g_outer->addWidget(range_, 0, 0);
  g_outer->addWidget(filter_, 1, 1);
  g_outer->addWidget(channels_,1,1,2,1);
  g_outer->addWidget(data_size_,0,1);

  // thresh

  act_thresh_ = new QDoubleSpinBox();
  act_thresh_->setSuffix(" g");
  act_thresh_->setAlignment(Qt::AlignRight);
  act_thresh_->setToolTip("Wakeup threshold");

  inact_thresh_ = new QDoubleSpinBox();
  inact_thresh_->setSuffix(" g");
  inact_thresh_->setAlignment(Qt::AlignRight);
  inact_thresh_->setToolTip("Sleep threshold");

  // inactive
  
  inactive_ = new QDoubleSpinBox();
  inactive_->setSuffix(" sec");
  inactive_->setAlignment(Qt::AlignRight);
  inactive_->setToolTip("Time below wakeup threshold to become inactive");

  // spinners

  f_lower->addRow("Active Threshold", act_thresh_);
  f_lower->addRow(&inact_thresh_label, inact_thresh_);
  f_lower->addRow("Inactivity", inactive_);

  spinners_->setLayout(f_lower);
  g_outer->addWidget(spinners_, 3, 0);

  configbox_->setLayout(g_outer);
  vbox_->addWidget(configbox_);

  // Track range to set threshold steps

  connect(range_, &PBEnumGroup::idClicked, this,
          &Adxl362Config::on_adxlrange_clicked);

  // Track sample rate to set time steps

  connect(sample_rate_, &PBEnumGroup::idClicked, this,
          &Adxl362Config::on_adxlfreq_clicked);
}

bool Adxl362Config::Attach(const Config &config)
{
  return SetConfig(config);
}

Adxl362Config::~Adxl362Config(){}

bool Adxl362Config::GetConfig(Config &config)
{
  // Parameters common to all tags

  int id;
  Adxl362 adxl(config.adxl362());

  if (sample_rate_)
    {
      id = sample_rate_->checkedId();
      if (Adxl362_Odr_IsValid(id))
        adxl.set_freq((Adxl362_Odr)id);
    }

  if (range_)
    {
      id = range_->checkedId();
      if (Adxl362_Rng_IsValid(id))
        adxl.set_range((Adxl362_Rng)id);
    }

  // set the accel type -- this should be deprecated

  adxl.set_accel_type(adxl_type);

  // set tag specific info

  switch (config.tag_type()) {
    case BITTAG:
      id = filter_->checkedId();
      if (Adxl362_Aa_IsValid(id)) {
          adxl.set_filter((Adxl362_Aa)id);
      } else {
          adxl.set_filter((Adxl362_Aa)0);
      }
      adxl.set_inact_thresh_g(inact_thresh_->value());
      // fall through to BITTAGNG for common part
    case BITTAGNG:
      adxl.set_inactive_sec(inactive_->value());
      adxl.set_act_thresh_g(act_thresh_->value());
      break;
    /*
    case ACCELTAG:
      id = channels_->checkedId();
      if ( Adxl367Channel_IsValid(id)) {
        adxl.set_channels((Adxl367Channel)id);
      }
      id = data_size_->checkedId();
      if (Adxl362_Adxl367DataFormat_IsValid(id)) {
        adxl.set_data_format((Adxl362_Adxl367DataFormat)id);
      }
      break;
    */
    default:
      break;
  }

  // set config to result

  config.set_allocated_adxl362(new Adxl362(adxl));
  return true;
}

bool Adxl362Config::SetConfig(const Config &config)
{
  // save accel type -- should be deprecated

  Adxl362 adxl(config.adxl362());
  adxl_type  = adxl.accel_type();

  // initialize all widgets default visibility

  filter_->setVisible(false);
  sample_rate_->setVisible(true);
  inact_thresh_->setVisible(false);
  inact_thresh_label.setVisible(false);
  data_size_->setVisible(false);
  channels_->setVisible(false);
  spinners_->setVisible(true);
  range_->setVisible(true);

  switch(config.tag_type()) {
    case BITTAG:
      filter_->setCheckedId((int)adxl.filter());
      filter_->setVisible(true);
      sample_rate_->setCheckedId((int)adxl.freq());
      range_->setCheckedId((int)adxl.range());
      inact_thresh_->setVisible(true);
      inact_thresh_label.setVisible(true);
      break;
    case BITTAGNG:
      sample_rate_->setCheckedId((int)adxl.freq());
      range_->setCheckedId((int)adxl.range());
      break;
    /*
    case ACCELTAGNG:
      sample_rate_->setCheckedId((int)adxl.freq());
      range_->setCheckedId((int)adxl.range());
      data_size_->setVisible(true);
      data_size_->setCheckedId((int)adxl.data_format());
      channels_->setVisible(true);
      channels_->setCheckedId((int)adxl.channels());
      spinners_->setVisible(false);
    */
    default:
      break;
  }

  // set legal range of spin boxes

  on_adxlfreq_clicked((int)adxl.freq());
  on_adxlrange_clicked((int)adxl.range());

  // adxl362 inactive_ time

  inactive_->setValue(static_cast<double>(adxl.inactive_sec()));

  // adxl362 threshold

  act_thresh_->setValue(static_cast<double>(adxl.act_thresh_g()));
  if (config.tag_type() == BITTAG){
     inact_thresh_->setValue(static_cast<double>(adxl.inact_thresh_g()));
  }
  active = true;
  return active;
}

// ADXL Range Buttons

void Adxl362Config::on_adxlrange_clicked(int index)
{
  double range;
  switch ((Adxl362_Rng)index)
  {
  case Adxl362_Rng_R2G:
    range = 1.0;
    break;
  case Adxl362_Rng_R4G:
    range = 2.0;
    break;
  case Adxl362_Rng_R8G:
    range = 4.0;
    break;
  default:
    range = 1.0;
  }

  act_thresh_->setRange(range / 64, range);
  act_thresh_->setSingleStep(range / 64);
  inact_thresh_->setRange(range / 64, range);
  inact_thresh_->setSingleStep(range / 64);
}

void Adxl362Config::on_adxlfreq_clicked(int index)
{
  double delta;
  switch ((Adxl362_Odr)index)
  {
  case Adxl362_Odr_S12_5:
    delta = 1 / 12.5;
    break;
  case Adxl362_Odr_S25:
    delta = 1 / 25.0;
    break;
  case Adxl362_Odr_S50:
    delta = 1 / 50.0;
    break;
  case Adxl362_Odr_S100:
    delta = 1 / 100.0;
    break;
  case Adxl362_Odr_S200:
    delta = 1 / 200.0;
    break;
  case Adxl362_Odr_S400:
    delta = 1 / 400.0;
    break;
  default:
    delta = 1 / 12.5;
  }
  inactive_->setRange(delta, delta * 255);
  inactive_->setSingleStep(delta);
}
