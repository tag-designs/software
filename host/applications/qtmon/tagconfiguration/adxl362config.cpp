
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

  spinners_ = new QGroupBox("Activity Detection");

  sample_rate_->setToolTip("Set Accelerometer Sample Rate");
  range_->setToolTip("Set Accelerometer Range");
  filter_->setToolTip("Set Accelerometer Anti-Alias Filter");
  spinners_->setToolTip("Set Activity Detection Parameters");
  // range and filter

  g_outer->addWidget(sample_rate_, 1, 0, 2, 1);
  g_outer->addWidget(range_, 0, 0);
  g_outer->addWidget(filter_, 1, 1);

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

  f_lower->addRow(&act_thresh_label, act_thresh_);
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

bool Adxl362Config::Attach(Tag &tag)
{
   return true;
   //return SetConfig(config);
}

Adxl362Config::~Adxl362Config(){}

bool Adxl362Config::GetConfig(Config &config)
{
  // Parameters common to all tags

  if (!active)
    return true;

  int id;
  Adxl362 adxl(config.adxl362());

  if (visibility_.adxl362_accel_type)
  {
    if (!isAdxl375)
    {
      adxl.set_accel_type(Adxl362_AdxlType_AdxlType_362);
    } else {
      adxl.set_accel_type(Adxl362_AdxlType_AdxlType_367);
    }
  }

  if (visibility_.adxl362_freq && sample_rate_ && sample_rate_->isVisible())
  {
    id = sample_rate_->checkedId();
    if (Adxl362_Odr_IsValid(id))
      adxl.set_freq((Adxl362_Odr)id);
  }

  if (visibility_.adxl362_range && range_ && range_->isVisible())
  {
    id = range_->checkedId();
    if (Adxl362_Rng_IsValid(id))
      adxl.set_range((Adxl362_Rng)id);
  }

  // set tag specific info

  if (visibility_.adxl362_filter && filter_ && filter_->isVisible()){
    id = filter_->checkedId();
    if (Adxl362_Aa_IsValid(id)) {
        adxl.set_filter((Adxl362_Aa)id);
    } else {
        adxl.set_filter((Adxl362_Aa)0);
    }
  }

  if (visibility_.adxl362_inact_thresh_g && inact_thresh_->isVisible()){
    adxl.set_inact_thresh_g(inact_thresh_->value());
  }
  if (visibility_.adxl362_inactive_sec) {
    adxl.set_inactive_sec(inactive_->value());
  }
  if (visibility_.adxl362_act_thresh_g) {
    adxl.set_act_thresh_g(act_thresh_->value());
  }


  // set config to result

  config.set_allocated_adxl362(new Adxl362(adxl));
  return true;
}

bool Adxl362Config::SetConfig(const Config &config)
{
  ConfigFieldVisibility visibility;
  visibility.adxl362 = config.has_adxl362();
  visibility.adxl362_range = config.has_adxl362() && (config.adxl362().range() != Adxl362_Rng_Rng_UNSPECIFIED);
  visibility.adxl362_freq = config.has_adxl362() && (config.adxl362().freq() != Adxl362_Odr_Odr_UNSPECIFIED);
  visibility.adxl362_filter = config.has_adxl362() && (config.adxl362().filter() != Adxl362_Aa_Aa_UNSPESIFIED);
  visibility.adxl362_act_thresh_g = config.has_adxl362() && (config.adxl362().act_thresh_g() != 0.0f);
  visibility.adxl362_inact_thresh_g = config.has_adxl362() && (config.adxl362().inact_thresh_g() != 0.0f);
  visibility.adxl362_inactive_sec = config.has_adxl362() && (config.adxl362().inactive_sec() != 0.0f);
  visibility.adxl362_accel_type = config.has_adxl362();
  visibility.adxl362_data_format = config.has_adxl362() && (config.adxl362().data_format() != Adxl362_Adxl367DataFormat_DF_UNSPECIFIED);
  visibility.adxl362_channels = config.has_adxl362() && (config.adxl362().channels() != AdxlChannel_UNSPECIFIED);
  visibility.adxl362_active_sec = config.has_adxl362() && (config.adxl362().active_sec() != 0.0f);
  return SetConfig(config, visibility);
}

bool Adxl362Config::SetConfig(const Config &config,
                              const ConfigFieldVisibility &visibility)
{
  visibility_ = visibility;

  if (!visibility_.adxl362)
  {
    active = false;
    setVisible(false);
    return true;
  }

  Adxl362 adxl(config.adxl362());

  isAdxl375 = adxl.accel_type() == Adxl362_AdxlType_AdxlType_367;

  // initialize all widgets default visibility

  filter_->setVisible(visibility_.adxl362_filter);
  sample_rate_->setVisible(visibility_.adxl362_freq);
  act_thresh_->setVisible(visibility_.adxl362_act_thresh_g);
  act_thresh_label.setVisible(visibility_.adxl362_act_thresh_g);
  inact_thresh_->setVisible(visibility_.adxl362_inact_thresh_g);
  inact_thresh_label.setVisible(visibility_.adxl362_inact_thresh_g);
  spinners_->setVisible(visibility_.adxl362_act_thresh_g ||
                        visibility_.adxl362_inact_thresh_g ||
                        visibility_.adxl362_inactive_sec);
  range_->setVisible(visibility_.adxl362_range);

   // adxl362 range, freq, and filter

  if (visibility_.adxl362_filter)
    filter_->setCheckedId((int)adxl.filter());
  if (visibility_.adxl362_freq)
    sample_rate_->setCheckedId((int)adxl.freq());
  if (visibility_.adxl362_range)
    range_->setCheckedId((int)adxl.range());

  if (visibility_.adxl362_freq)
    on_adxlfreq_clicked((int)adxl.freq());
  if (visibility_.adxl362_range)
    on_adxlrange_clicked((int)adxl.range());

  if (!visibility_.adxl362_range)
    on_adxlrange_clicked(Adxl362_Rng_R2G);
  if (!visibility_.adxl362_freq)
    on_adxlfreq_clicked(Adxl362_Odr_S12_5);

  if (visibility_.adxl362_inact_thresh_g)
    inact_thresh_->setValue(static_cast<double>(adxl.inact_thresh_g()));

  // adxl362 inactive_ time

  if (visibility_.adxl362_inactive_sec)
    inactive_->setValue(static_cast<double>(adxl.inactive_sec()));

  // adxl362 threshold

  if (visibility_.adxl362_act_thresh_g)
    act_thresh_->setValue(static_cast<double>(adxl.act_thresh_g()));


  active = true;
  setVisible(true);
  return true;
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
