/**
 * @file    tagstatusstrip.cpp
 * @brief   The status strip between qtmonitor's tab bar and its pages.
 */

#include "tagstatusstrip.h"

#include <QAction>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace
{

/**
 * @brief A caption and a value, side by side, for one status item.
 * @param[in,out] row      Layout the caption, value and trailing spacing are
 *                         appended to; it takes ownership of both labels.
 * @param[in]     caption  Dimmed label text naming the item.
 * @param[in]     tooltip  Tooltip shown on both the caption and the value.
 * @param[in] widest  The longest value the item shows; the value is at least
 *                    this wide, so the window does not change width as the
 *                    tag's state changes.
 * @return  The value label, initially empty, for the caller to update.
 */
QLabel *addItem(QHBoxLayout *row, const QString &caption, const QString &tooltip,
                const QString &widest)
{
  auto *name = new QLabel(caption);
  name->setStyleSheet("color: palette(mid);");
  name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  auto *value = new QLabel();
  value->setTextInteractionFlags(Qt::TextSelectableByMouse);
  value->setMinimumWidth(value->fontMetrics().horizontalAdvance(widest));
  name->setToolTip(tooltip);
  value->setToolTip(tooltip);
  row->addWidget(name);
  row->addWidget(value);
  row->addSpacing(10);
  return value;
}

/// A tool button showing @p action's text, with the look of a push button.
QToolButton *actionButton(QAction *action)
{
  auto *button = new QToolButton();
  button->setDefaultAction(action);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setMinimumWidth(70);
  return button;
}

} // namespace

TagStatusStrip::TagStatusStrip(QAction *start, QAction *stop, QWidget *parent)
    : QFrame(parent)
{
  setFrameShape(QFrame::StyledPanel);
  // One line, whatever the window's height: spare height goes to the page.
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto *row = new QHBoxLayout(this);
  row->setContentsMargins(10, 4, 10, 4);

  link_ = new QLabel();
  link_->setTextFormat(Qt::RichText);
  link_->setToolTip(tr("Whether qtmonitor is attached to a tag"));
  link_->setMinimumWidth(link_->fontMetrics().horizontalAdvance("\u25CF Detached") + 4);
  row->addWidget(link_);
  row->addSpacing(10);
  tag_ = addItem(row, tr("Type"), tr("Attached tag type and board UUID"),
                 "COMPASSTAG");
  state_ = addItem(row, tr("State"), tr("Tag state"), "HIBERNATING");
  clock_ = addItem(row, tr("Clock"),
                   tr("Tag clock minus this computer's clock, in seconds"),
                   "-1.45e+09 s");
  voltage_ = addItem(row, tr("Voltage"), tr("Tag supply voltage"), "0.00 V");
  test_ = addItem(row, tr("Self-test"), tr("Self-test result the tag reports"),
                  "EXT_FLASH_FAILED");
  row->addStretch(1);

  approved_ = new QCheckBox(tr("Approved"));
  approved_->setToolTip(
      tr("Tick when you have reviewed the configuration. Start warns if it is not "
         "ticked; any change to the configuration clears it."));
  row->addWidget(approved_);
  row->addSpacing(6);

  start_ = actionButton(start);
  stop_ = actionButton(stop);
  row->addWidget(start_);
  row->addWidget(stop_);

  setDetached();
}

void TagStatusStrip::setAttached(TagType tag_type, const QString &uuid)
{
  link_->setText(tr("<span style=\"color:#2e9d4a;\">\u25CF</span> <b>Attached</b>"));
  tag_->setText(QString::fromStdString(std::string(TagType_Name(tag_type))));
  tag_->setToolTip(uuid.isEmpty() ? tr("Attached tag type")
                                  : tr("Attached tag type; board UUID %1").arg(uuid));
}

void TagStatusStrip::setDetached()
{
  link_->setText(tr("<span style=\"color:#c62828;\">\u25CF</span> <b>Detached</b>"));
  tag_->clear();
  tag_->setToolTip(tr("Attached tag type and board UUID"));
  state_->clear();
  clock_->clear();
  clock_->setStyleSheet(QString());
  voltage_->clear();
  test_->clear();
  test_->setStyleSheet(QString());
}

void TagStatusStrip::setStatus(TagState state, double clock_offset_s,
                               float voltage, TestResult test)
{
  state_->setText(QString::fromStdString(std::string(TagState_Name(state))));
  // Hundredths for a set clock; three figures for one far off (an unset
  // clock is about -1.45e+09 s), so the text stays short.
  clock_->setText(qAbs(clock_offset_s) < 1000.0
                      ? QString::asprintf("%+.2f s", clock_offset_s)
                      : QString::asprintf("%+.3g s", clock_offset_s));
  // Red marks what Start would warn about.
  const QString warn = QStringLiteral("color: #c62828; font-weight: bold;");
  clock_->setStyleSheet(qAbs(clock_offset_s) > kClockToleranceS ? warn : QString());
  test_->setStyleSheet(test != ALL_PASSED ? warn : QString());
  voltage_->setText(QString::number(static_cast<double>(voltage), 'f', 2) + " V");
  test_->setText(QString::fromStdString(std::string(TestResult_Name(test))));
}

void TagStatusStrip::setTestRunning()
{
  test_->setText(tr("Running"));
  test_->setStyleSheet(QString());
}

bool TagStatusStrip::isApproved() const
{
  return approved_->isChecked();
}

void TagStatusStrip::setApproved(bool approved)
{
  approved_->setChecked(approved);
}

void TagStatusStrip::setApprovalAvailable(bool available)
{
  approved_->setEnabled(available);
  if (!available)
    approved_->setChecked(false);
}
