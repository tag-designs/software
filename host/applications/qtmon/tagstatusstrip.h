/**
 * @file    tagstatusstrip.h
 * @brief   The strip below qtmonitor's tab bar: whether a tag is attached, its
 *          type, state, clock offset, voltage and self-test result, with the
 *          Start and Stop buttons, and the "Approved" box Start checks.
 *
 * @details The tag's board UUID is in the tag type's tooltip, and on the Tag
 *          page. It is visible from every tab, so the tag's state is never out of
 *          sight when starting or stopping it. Start and Stop are tool buttons
 *          bound to MainWindow's actions, so they share the actions' enabled
 *          state and the menus' behaviour.
 */

#ifndef TAGSTATUSSTRIP_H
#define TAGSTATUSSTRIP_H

#include <QFrame>
#include <QString>

#include "tag.pb.h"

class QAction;
class QCheckBox;
class QLabel;
class QToolButton;

/**
 * @class   TagStatusStrip
 * @brief   One-line status of the attached tag, with Start and Stop.
 */
class TagStatusStrip : public QFrame
{
  Q_OBJECT

public:
  /**
   * @param[in] start   Action the Start button triggers and mirrors.
   * @param[in] stop    Action the Stop button triggers and mirrors.
   * @param[in] parent  Qt parent.
   */
  TagStatusStrip(QAction *start, QAction *stop, QWidget *parent = nullptr);

  /// Shows "Attached", and the tag's type with its board UUID as tooltip.
  void setAttached(TagType tag_type, const QString &uuid);
  /// Shows "Detached", and clears the type and status fields.
  void setDetached();

  /**
   * @brief Shows one status poll.
   * @param[in] state          Tag state.
   * @param[in] clock_offset_s Tag clock minus host clock, in seconds.
   * @param[in] voltage        Tag voltage, in volts.
   * @param[in] test           Self-test result the tag reports.
   */
  void setStatus(TagState state, double clock_offset_s, float voltage,
                 TestResult test);

  /// Shows the self-test as running until the next setStatus().
  void setTestRunning();

  /// Whether "Approved" is ticked.
  bool isApproved() const;
  /// Ticks or clears "Approved".
  void setApproved(bool approved);
  /// Enables "Approved" (there is a configuration to approve) or disables
  /// and clears it.
  void setApprovalAvailable(bool available);

private:
  QLabel *link_ = nullptr;  ///< Attached (green marker) or Detached (red).
  QLabel *tag_ = nullptr;      ///< Tag type; the board UUID is its tooltip.
  QLabel *state_ = nullptr;    ///< Tag state.
  QLabel *clock_ = nullptr;    ///< Clock offset; red beyond qtmon::kClockToleranceS.
  QLabel *voltage_ = nullptr;  ///< Supply voltage.
  QLabel *test_ = nullptr;     ///< Self-test result, or "Running"; red unless ALL_PASSED.
  QCheckBox *approved_ = nullptr;  ///< "Approved": the user reviewed the configuration.
  QToolButton *start_ = nullptr;   ///< Bound to MainWindow's Start action.
  QToolButton *stop_ = nullptr;    ///< Bound to MainWindow's Stop action.
};

#endif // TAGSTATUSSTRIP_H
