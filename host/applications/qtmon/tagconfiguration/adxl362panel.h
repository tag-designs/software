/**
 * @file    adxl362panel.h
 * @brief   Accelerometer configuration panel, in one fixed variant per meaning
 *          of the `adxl362` fields.
 */

#ifndef ADXL362PANEL_H
#define ADXL362PANEL_H

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>

#include "configpanel.h"
#include "pbenumgroup.h"

/**
 * @class   Adxl362Panel
 * @brief   Edits `Config.adxl362`, whose fields several tag families reuse
 *          with different meanings.
 *
 * @details Each meaning is its own fixed variant, chosen at construction, with
 *          its own title, labels, ranges and tooltips:
 *
 *          | Variant  | Tags                  | Controls |
 *          | -------- | --------------------- | -------- |
 *          | Standard | BitTag                | sample rate, range, filter; thresholds in g; inactivity in seconds. The threshold step follows the range and the inactivity step follows the sample rate. |
 *          | Wake362  | BitTag LE, BitPresTag | wake-mode ADXL362 at a fixed 4 g range: active and inactive thresholds in g; inactivity as 1-255 samples |
 *          | Wake367  | BitTagNG, UIUCTag     | ADXL367 wake-up: threshold 0.2-0.7 g; inactivity as 3-12 samples at 6.25 Hz |
 *
 *          BitTagNG also reports `adxl362.accel_type`; its panel is built with
 *          @p owns_accel_type, and writes the type back as ADXL367 when the tag
 *          reported ADXL367 and as ADXL362 otherwise.
 *
 *          The semantic reuse is documented in each family's firmware
 *          `config.c` and in tagconfiguration/README.md.
 */
class Adxl362Panel : public ConfigPanel
{
  Q_OBJECT

public:
  /// Which meaning of the adxl362 fields the panel edits.
  enum class Variant
  {
    Standard,  ///< classic BitTag
    Wake362,   ///< BitTag LE and BitPresTag wake mode
    Wake367,   ///< BitTagNG and UIUCTag ADXL367 wake-up
  };

  /**
   * @param[in] variant          Meaning of the fields; fixed.
   * @param[in] owns_accel_type  true to also write `adxl362.accel_type`
   *                             (BitTagNG). Only meaningful for Wake367.
   * @param[in] parent           Qt parent.
   */
  Adxl362Panel(Variant variant, bool owns_accel_type, QWidget *parent = nullptr);

  std::vector<std::string_view> paths() const override;
  bool load(const Config &config) override;
  bool store(Config &config) const override;

private slots:
  /// Standard only: sizes the threshold controls for the selected range.
  void on_adxlrange_clicked(int index);
  /// Standard only: sizes the inactivity control for the selected rate.
  void on_adxlfreq_clicked(int index);

private:
  Variant variant_;
  bool owns_accel_type_;
  bool has_inact_thresh_;      ///< the inactive threshold is a control
  bool reported_adxl367_ = false;  ///< the last load() saw accel_type ADXL367

  QGroupBox *configbox_ = nullptr;
  PBEnumGroup *sample_rate_ = nullptr;  ///< Standard only
  PBEnumGroup *range_ = nullptr;        ///< Standard only
  PBEnumGroup *filter_ = nullptr;       ///< Standard only
  QGroupBox *spinners_ = nullptr;
  QDoubleSpinBox *act_thresh_ = nullptr;
  QDoubleSpinBox *inact_thresh_ = nullptr;
  QDoubleSpinBox *inactive_ = nullptr;
  QLabel *act_thresh_label_ = nullptr;
  QLabel *inact_thresh_label_ = nullptr;
  QLabel *inactive_label_ = nullptr;
};

#endif // ADXL362PANEL_H
