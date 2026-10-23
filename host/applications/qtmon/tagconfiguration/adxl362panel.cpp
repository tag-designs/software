/**
 * @file    adxl362panel.cpp
 * @brief   The accelerometer panel's three variants.
 */

#include "adxl362panel.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QVBoxLayout>

/// Range buttons, ids from Adxl362::Rng (Standard variant only).
static const std::vector<PBEnumGroup::binfo> adxl_range_buttons{
    {Adxl362_Rng_R2G, "+/- 2g", nullptr},
    {Adxl362_Rng_R4G, "+/- 4g", nullptr},
    {Adxl362_Rng_R8G, "+/- 8g", nullptr}};

/// Sample rate buttons, ids from Adxl362::Odr (Standard variant only).
static const std::vector<PBEnumGroup::binfo> adxl_odr_buttons{
    {Adxl362_Odr_S12_5, "12.5 Hz", "12.5 samples/second"},
    {Adxl362_Odr_S25, "25 Hz", "25 samples/second"},
    {Adxl362_Odr_S50, "50 Hz", "50 samples/second"},
    {Adxl362_Odr_S100, "100 Hz", "100 samples/second"},
    {Adxl362_Odr_S200, "200 Hz", "200 samples/second"},
    {Adxl362_Odr_S400, "400 Hz", "400 samples/second"}};

/// Anti-alias filter buttons, ids from Adxl362::Aa (Standard variant only).
static const std::vector<PBEnumGroup::binfo> adxl_filter_buttons{
    {Adxl362_Aa_AAquarter, "1/4 Sample Rate", "filter cutoff -- 1/4 sample rate"},
    {Adxl362_Aa_AAhalf, "1/2 Sample Rate", "filter cutoff -- 1/2 sample rate"}};

Adxl362Panel::Adxl362Panel(Variant variant, bool owns_accel_type, QWidget *parent)
    : ConfigPanel(parent), variant_(variant),
      owns_accel_type_(variant == Variant::Wake367 && owns_accel_type),
      has_inact_thresh_(variant != Variant::Wake367)
{
  auto *vbox = new QVBoxLayout();
  setLayout(vbox);

  configbox_ = new QGroupBox("Accelerometer");
  auto *g_outer = new QGridLayout;
  auto *f_lower = new QFormLayout;

  if (variant_ == Variant::Standard)
  {
    sample_rate_ = new PBEnumGroup("Sample Rate", adxl_odr_buttons);
    range_ = new PBEnumGroup("Range", adxl_range_buttons);
    filter_ = new PBEnumGroup("Filter", adxl_filter_buttons);
    sample_rate_->setToolTip("Set Accelerometer Sample Rate");
    range_->setToolTip("Set Accelerometer Range");
    filter_->setToolTip("Set Accelerometer Anti-Alias Filter");
    // One row of three, so the panel is short enough for a laptop screen.
    g_outer->addWidget(range_, 0, 0);
    g_outer->addWidget(sample_rate_, 0, 1);
    g_outer->addWidget(filter_, 0, 2);
  }

  spinners_ = new QGroupBox("Activity Detection");
  spinners_->setToolTip("Set Activity Detection Parameters");

  act_thresh_ = new QDoubleSpinBox();
  act_thresh_->setSuffix(" g");
  act_thresh_->setAlignment(Qt::AlignRight);
  act_thresh_->setToolTip("Wakeup threshold");

  inact_thresh_ = new QDoubleSpinBox();
  inact_thresh_->setSuffix(" g");
  inact_thresh_->setAlignment(Qt::AlignRight);
  inact_thresh_->setToolTip("Sleep threshold");

  inactive_ = new QDoubleSpinBox();
  inactive_->setSuffix(" sec");
  inactive_->setAlignment(Qt::AlignRight);
  inactive_->setToolTip("Time below wakeup threshold to become inactive");

  act_thresh_label_ = new QLabel("Active Threshold");
  inact_thresh_label_ = new QLabel("Inactive Threshold");
  inactive_label_ = new QLabel("Inactivity");

  f_lower->addRow(act_thresh_label_, act_thresh_);
  f_lower->addRow(inact_thresh_label_, inact_thresh_);   // hidden for Wake367
  f_lower->addRow(inactive_label_, inactive_);

  spinners_->setLayout(f_lower);
  g_outer->addWidget(spinners_, 1, 0, 1, 3);
  configbox_->setLayout(g_outer);
  vbox->addWidget(configbox_);

  // Fixed per variant. The order of range, step and decimals calls matters:
  // QDoubleSpinBox rounds its limits to the decimals in force.
  switch (variant_)
  {
  case Variant::Standard:
    connect(range_, &PBEnumGroup::idClicked, this, &Adxl362Panel::on_adxlrange_clicked);
    connect(sample_rate_, &PBEnumGroup::idClicked, this, &Adxl362Panel::on_adxlfreq_clicked);
    act_thresh_->setDecimals(2);
    inactive_->setDecimals(2);
    break;

  case Variant::Wake362:
    // The firmware fixes the range at 4 g and samples at the wake rate.
    on_adxlrange_clicked(Adxl362_Rng_R4G);
    on_adxlfreq_clicked(Adxl362_Odr_S12_5);
    configbox_->setTitle("Activity Wakeup");
    inactive_label_->setText("Inactivity Samples");
    act_thresh_->setDecimals(2);
    inact_thresh_->setDecimals(2);
    inactive_->setRange(1.0, 255.0);
    inactive_->setSingleStep(1.0);
    inactive_->setDecimals(0);
    inactive_->setSuffix(" samples");
    act_thresh_->setToolTip("ADXL362 wake-up active threshold in g");
    inact_thresh_->setToolTip("ADXL362 wake-up inactive threshold in g");
    inactive_->setToolTip("Samples below inactive threshold at 6 Hz");
    break;

  case Variant::Wake367:
    // Raw-milligram activity threshold; inactivity as a sample count at the
    // ADXL367's 6.25 Hz wake rate (see BitTagNG's and UIUCTag's config.c).
    on_adxlrange_clicked(Adxl362_Rng_R2G);
    on_adxlfreq_clicked(Adxl362_Odr_S12_5);
    configbox_->setTitle("Activity Wakeup");
    act_thresh_label_->setText("Wakeup Threshold");
    inactive_label_->setText("Inactivity Samples");
    act_thresh_->setRange(0.2, 0.7);
    act_thresh_->setSingleStep(0.05);
    act_thresh_->setDecimals(2);
    inactive_->setRange(3.0, 12.0);
    inactive_->setSingleStep(1.0);
    inactive_->setDecimals(0);
    inactive_->setSuffix(" samples");
    act_thresh_->setToolTip("ADXL367 wake-up threshold in g");
    inactive_->setToolTip(
        "Samples below the inactivity threshold before the ADXL367 declares "
        "inactivity. Counted at its 6.25 Hz wake rate, so one sample is "
        "~160 ms (e.g. 3 samples ≈ 0.48 s, 12 samples ≈ 1.92 s) -- "
        "not seconds directly.");
    break;
  }

  if (!has_inact_thresh_)
  {
    inact_thresh_->setVisible(false);
    inact_thresh_label_->setVisible(false);
  }
}

std::vector<std::string_view> Adxl362Panel::paths() const
{
  std::vector<std::string_view> owned = {"adxl362.act_thresh_g", "adxl362.inactive_sec"};
  if (has_inact_thresh_)
    owned.push_back("adxl362.inact_thresh_g");
  if (variant_ == Variant::Standard)
  {
    owned.push_back("adxl362.range");
    owned.push_back("adxl362.freq");
    owned.push_back("adxl362.filter");
  }
  if (owns_accel_type_)
    owned.push_back("adxl362.accel_type");
  return owned;
}

bool Adxl362Panel::load(const Config &config)
{
  const Adxl362 &adxl = config.adxl362();
  reported_adxl367_ = adxl.accel_type() == Adxl362_AdxlType_AdxlType_367;

  if (variant_ == Variant::Standard)
  {
    filter_->setCheckedId((int)adxl.filter());
    sample_rate_->setCheckedId((int)adxl.freq());
    range_->setCheckedId((int)adxl.range());
    on_adxlfreq_clicked((int)adxl.freq());
    on_adxlrange_clicked((int)adxl.range());
  }

  // Values after ranges, so out-of-range values are clamped as displayed.
  if (has_inact_thresh_)
    inact_thresh_->setValue(static_cast<double>(adxl.inact_thresh_g()));
  inactive_->setValue(static_cast<double>(adxl.inactive_sec()));
  act_thresh_->setValue(static_cast<double>(adxl.act_thresh_g()));
  return true;
}

bool Adxl362Panel::store(Config &config) const
{
  Adxl362 adxl(config.adxl362());
  int id;

  if (owns_accel_type_)
    adxl.set_accel_type(reported_adxl367_ ? Adxl362_AdxlType_AdxlType_367
                                          : Adxl362_AdxlType_AdxlType_362);

  if (variant_ == Variant::Standard)
  {
    id = sample_rate_->checkedId();
    if (Adxl362_Odr_IsValid(id))
      adxl.set_freq((Adxl362_Odr)id);

    id = range_->checkedId();
    if (Adxl362_Rng_IsValid(id))
      adxl.set_range((Adxl362_Rng)id);

    id = filter_->checkedId();
    adxl.set_filter(Adxl362_Aa_IsValid(id) ? (Adxl362_Aa)id : (Adxl362_Aa)0);
  }

  if (has_inact_thresh_)
    adxl.set_inact_thresh_g(inact_thresh_->value());
  adxl.set_inactive_sec(inactive_->value());
  adxl.set_act_thresh_g(act_thresh_->value());

  *config.mutable_adxl362() = adxl;
  return true;
}

void Adxl362Panel::on_adxlrange_clicked(int index)
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

void Adxl362Panel::on_adxlfreq_clicked(int index)
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
