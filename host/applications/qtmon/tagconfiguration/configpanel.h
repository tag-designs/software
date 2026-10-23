/**
 * @file    configpanel.h
 * @brief   Base class for one fixed group of configuration controls.
 *
 * @details The Configuration tab is a page of panels. Every panel is built
 *          once, when the tab is constructed, with its titles, labels, ranges
 *          and tooltips fixed for the tags it serves. The attached tag's type
 *          selects which panels are shown (ConfigTab's panel table). A panel
 *          is shown or hidden as a whole: one selected for a tag matches that
 *          tag, so it never decides per widget what to show.
 *
 *          Each panel owns a fixed set of Config fields, listed by paths().
 *          load() reads exactly those fields into its controls, and store()
 *          writes exactly those fields back, whatever is on screen. Fields no
 *          panel owns pass through from the tag's own configuration unchanged.
 *
 * @see     docs/shared/design/configuration-model.md, tagconfiguration/README.md
 */

#ifndef CONFIGPANEL_H
#define CONFIGPANEL_H

#include <string_view>
#include <vector>

#include <QWidget>

#include "tag.pb.h"

/**
 * @class   ConfigPanel
 * @brief   One fixed group of configuration controls owning a set of fields.
 */
class ConfigPanel : public QWidget
{
public:
  using QWidget::QWidget;

  /**
   * @brief   The Config fields this panel reads and writes, as ConfigShape
   *          paths. `qtmonitor --check-config-panels` checks them against
   *          each tag type's ConfigShape.
   */
  virtual std::vector<std::string_view> paths() const = 0;

  /**
   * @brief   Fills the controls from @p config.
   *
   * @param[in] config  The configuration to display; only paths() are read.
   * @return  true when the panel applies to this configuration and should be
   *          shown; false when it should stay hidden and store() should not be
   *          called. Only the BitTag log panel returns false, when the tag
   *          reports no log format it recognises.
   */
  virtual bool load(const Config &config) = 0;

  /**
   * @brief   Writes the controls' values into @p config.
   *
   * @param[in,out] config  Configuration to update; only paths() are written.
   * @return  false when a control holds a value that cannot be written, in
   *          which case @p config may be partly updated and must not be sent.
   */
  virtual bool store(Config &config) const = 0;
};

#endif // CONFIGPANEL_H
