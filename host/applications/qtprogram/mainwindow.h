/**
 * @file    mainwindow.h
 * @brief   Tag Programmer's window: the station panel, the tag panel, the
 *          firmware and tag type choices, and the dialogs Station needs.
 *
 * @details The window displays a Station (station.h) and asks the operator
 *          what Station cannot decide: which base, where the record is,
 *          whether attaching may reset tags, and each tag's label. It holds no
 *          programming logic. The layout is built in code because its rows
 *          follow Station::Check. See design/batch-station.md.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <array>

#include "station.h"

class QAction;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QToolButton;
class LogWindow;

/// The log pane's text box, owned by LogWindow; main.cpp's message handlers write to it.
extern QTextEdit *s_textEdit;

/**
 * @class   MainWindow
 * @brief   Tag Programmer's main window.
 *
 * @details Construction builds the widgets and restores nothing itself;
 *          start() runs the station's checks once the window is showing, so
 *          their dialogs have a parent on screen. GUI thread only.
 */
class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  /// Builds the window around a new Station.
  explicit MainWindow(QWidget *parent = nullptr);

  /**
   * @brief Starts the station: asks once whether attaching may reset tags,
   *        then runs the checks.
   */
  void start();

  /**
   * @brief Captures the user guide's screenshots, then exits the application.
   *
   * @details Writes `qtprogram-station.png` once the checks have settled, then
   *          opens the Program dialog, writes `qtprogram-program.png` and
   *          cancels it. Nothing is erased or programmed, though the checks
   *          attach to the tag, which resets it, when attaching automatically
   *          is allowed.
   *
   * @param[in] dir  Directory for the PNG files.
   */
  void captureScreenshots(const QString &dir);

  /**
   * @brief Bench smoke test: runs the checks, programs the tag as Program
   *        does (without the dialog) under @p label, and exits.
   *
   * @details Prints each step and the row recorded. If the new image does not
   *          answer, retries every 5 s for 3 minutes so the base can be
   *          replugged. Exits 0 when the result is OK, 1 otherwise, and 2 when
   *          the station could not program at all.
   *
   * @param[in] label  The label recorded for the tag.
   * @warning Erases and programs the tag on the base.
   */
  void benchProgram(const QString &label);

private:
  /// One station panel row.
  struct CheckRow
  {
    QLabel *light = nullptr;  ///< The coloured dot.
    QLabel *text = nullptr;   ///< The check's line.
  };

  /// Builds the widgets.
  void buildUi();
  /// Redraws one station panel row.
  void showCheck(Station::Check check);
  /// Refills the firmware drop-down from the catalog and the choice.
  void showFirmware();
  /// Refills the tag panel.
  void showTag();
  /// Enables the buttons for the current state.
  void updateButtons();
  /// Updates the batch counter.
  void showBatch();
  /// The firmware drop-down changed to @p index.
  void firmwareChosen(int index);
  /// Asks for the STM32CubeProgrammer folder.
  void chooseProgrammer();
  /// Asks which base to use.
  void chooseBase();
  /// Asks to create or choose the record.
  void chooseRecord();
  /// Program: asks for the label and confirms, then starts the session.
  void programClicked();
  /// Retest: asks for the label, then runs the self-tests again.
  void retestClicked();
  /// Shows a session step.
  void sessionStep(ProgramSession::Step step, const QString &text);
  /// Shows a finished program or retest.
  void programFinished(const ProgramRecord &record, bool written, const QString &message);
  /**
   * @brief The Program dialog: label, tag, image, erase, and what is lost
   *        and kept.
   * @param[in,out] label    The label typed; starts empty.
   * @param[in]     capture  When non-empty, saves the dialog there and cancels it.
   * @return  true to program.
   */
  bool confirmProgram(QString &label, const QString &capture = QString());

  Station station_;             ///< Everything shown here.
  QComboBox *firmwareBox_ = nullptr;   ///< Releases, recent files, "Other ELF file...".
  QPushButton *notesButton_ = nullptr; ///< Opens the release page.
  QComboBox *targetBox_ = nullptr;     ///< Build targets.
  QLabel *targetNote_ = nullptr;       ///< "detected from the tag", or why the box is fixed.
  std::array<CheckRow, Station::kCheckCount> rows_; ///< The station panel.
  QPushButton *programmerButton_ = nullptr; ///< Choose the programmer folder.
  QPushButton *recordButton_ = nullptr;     ///< Create or choose the record.
  QPushButton *stopButton_ = nullptr;       ///< Stop a deployed tag.
  QPushButton *eraseButton_ = nullptr;      ///< Erase a FINISHED or ABORTED tag.
  QLabel *labelValue_ = nullptr;   ///< Tag panel: last label.
  QLabel *uuidValue_ = nullptr;    ///< Tag panel: UUID.
  QLabel *stateValue_ = nullptr;   ///< Tag panel: state.
  QLabel *firmwareValue_ = nullptr; ///< Tag panel: firmware and commit.
  QLabel *testsValue_ = nullptr;   ///< Tag panel: self-test result.
  QLabel *clockValue_ = nullptr;   ///< Tag panel: tag clock minus this computer's.
  QPushButton *syncButton_ = nullptr; ///< Tag panel: set the tag's clock.
  QLabel *resultValue_ = nullptr;  ///< Tag panel: last result.
  QPushButton *programButton_ = nullptr; ///< Program.
  QPushButton *retestButton_ = nullptr;  ///< Retest.
  QPushButton *nextButton_ = nullptr;    ///< Next tag.
  QPushButton *attachButton_ = nullptr;  ///< Attach.
  QPushButton *showCsvButton_ = nullptr; ///< Show CSV.
  QLabel *stepLabel_ = nullptr;          ///< The session's current step.
  QProgressBar *progress_ = nullptr;     ///< The tag's erase, or busy.
  QPushButton *retryButton_ = nullptr;   ///< Retry after a power cycle.
  QPushButton *cancelButton_ = nullptr;  ///< Cancel the session where possible.
  QLabel *batchLabel_ = nullptr;         ///< "Batch: N programmed, M failed".
  QToolButton *logToggle_ = nullptr;     ///< Shows or hides the log.
  QAction *autoAttachAction_ = nullptr; ///< Station > Attach Automatically.
  LogWindow *log_ = nullptr;             ///< The log pane.
  bool filling_ = false;                 ///< Refilling a drop-down; ignore its signals.
};

#endif // MAINWINDOW_H
