/**
 * @file    bittaglogpanel.h
 * @brief   Configuration panel for the BitTag internal log format.
 */

#ifndef BITTAGLOGPANEL_H
#define BITTAGLOGPANEL_H

#include "configpanel.h"

class PBEnumGroup;

/**
 * @class   BitTagLogPanel
 * @brief   Chooses `bittag_log`, the BitTag activity log format.
 *
 * @details load() hides the panel when the tag reports a format the panel has
 *          no button for, and for `BITTAG_UNSPECIFIED` on a classic BitTag it
 *          also warns, because that tag cannot log without one.
 */
class BitTagLogPanel : public ConfigPanel
{
public:
  /**
   * @brief Builds the log format buttons.
   * @param[in] parent  Qt parent.
   */
  explicit BitTagLogPanel(QWidget *parent = nullptr);

  std::vector<std::string_view> paths() const override;
  bool load(const Config &config) override;
  bool store(Config &config) const override;

private:
  PBEnumGroup *log_ = nullptr;
};

#endif // BITTAGLOGPANEL_H
