/**
 * @file    mainwindow.h
 * @brief   Tag Programmer's main window: base discovery, tag status, and the
 *          erase-program-test sequence.
 *
 * @details MainWindow finds a USB tag base, shows the attached tag's status
 *          and identity, and programs a firmware ELF through
 *          STM32_Programmer_CLI. Programming is a state machine
 *          (programStateMachine()) run once a second from a timer, so the GUI
 *          keeps polling the tag while it waits for the tag to stop, erase
 *          its log, and come back after programming. The internal-flash erase
 *          comes from the image (FlashPlan, flashplan.h). See README.md in
 *          this directory.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include <QProcess>
#include "tagclass.h"
#include "ui_mainwindow.h"
#include "flashplan.h"

class QWidget;
class QTextEdit;

/// The log pane's text box, owned by LogWindow; main.cpp's message handlers write to it.
extern QTextEdit *s_textEdit;
class LogScreen;

/**
 * @enum    ProgrammingState
 * @brief   Step of programStateMachine(), reported to the progress dialog.
 */
 enum class ProgrammingState {
     READY,        ///< About to begin; stops a running tag.
     STOPPING,     ///< Waiting for the tag to stop, then asking it to erase its log.
     ERASING,      ///< Waiting for the tag's own erase to return it to IDLE.
     PROGRAMMING,  ///< STM32_Programmer_CLI is erasing internal flash and writing the image.
     CONNECTING,   ///< Waiting for the newly programmed tag to answer.
     TESTING,      ///< The tag is running its self-tests.
     FINISHED,     ///< Done; the tag is erased, IDLE and detached.
     FAILED        ///< Stopped; MainWindow::failure_ says why.
  };

namespace Ui {
class MainWindow;
}

/**
 * @class   MainWindow
 * @brief   Tag Programmer's window and its programming sequence.
 *
 * @details Construction looks for a base (findBase()), finds the programmer
 *          (findProgrammer()) and restores the last image, but does not
 *          attach: attaching resets a tag, which would end a running
 *          deployment. Connect and Program attach. Holds one Tag connection;
 *          not thread-safe, GUI thread only.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  /**
   * @brief Builds the window, its Help menu and status, and looks for a base.
   * @param[in] parent  Qt parent.
   * @note  With several bases this asks which to use, before the window shows.
   */
  explicit MainWindow(QWidget *parent = nullptr);
  /// Detaches from the tag, if attached.
  ~MainWindow();

  /**
   * @brief Captures the user guide's screenshots, then exits the application.
   *
   * @details Writes `qtprogram-startup.png` (not connected), then presses
   *          Connect and writes `qtprogram-connected.png`, then opens the
   *          Program confirmation for @p image, writes
   *          `qtprogram-confirm.png` and cancels it. Nothing is erased or
   *          programmed. Connecting resets the tag, as Connect always does.
   *
   * @param[in] dir    Directory for the PNG files.
   * @param[in] image  Firmware ELF named in the confirmation.
   */
  void captureScreenshots(const QString &dir, const QString &image);
 

signals:

  /// Declared for a tag-state listener; nothing emits or connects it.
  void StateUpdate(TagState state);
  /// Emitted by programStateMachine() after each step; drives the progress dialog's text.
  void ProgrammingStateUpdate(ProgrammingState);

#ifndef __arm64__
  /// Left from an earlier programming library's callbacks; unused.
  void lBar(int currProgress, int total);
  /// Left from an earlier programming library's callbacks; unused.
  void DisplayMessage(int msgType, QString str);
  /// Left from an earlier programming library's callbacks; unused.
  void logMessage(int msgType, QString str);
#endif

  /// Emitted by each poll while the tag is erasing (sRESET), with the sectors erased so far.
  void SectorsErased(int);
  /// Emitted by each poll that finds the tag IDLE; nothing connects to it at present.
  void IdleState(void);


private slots:

  // attach/detach to tag

  /**
   * @brief Attaches to the tag on the selected base and fills the status and
   *        identity fields, then polls every 400 ms.
   * @return true when attached; false when already attached or the tag does
   *         not answer.
   * @warning Attaching resets the tag, ending a running deployment.
   * @pre   findBase() has selected a base (usbdev).
   */
  bool Attach();
  /// Stops polling, detaches and greys out the tag fields.
  void Detach();

  // timer tick

  /**
   * @brief One status poll: voltage, state, clock error and self-test result.
   * @details Updates current_state, which programStateMachine() reads, and
   *          emits IdleState() or SectorsErased() as the state calls for.
   */
  void TriggerUpdate();

  // control tab events

  /**
   * @brief Program Tag: checks the programmer and image, plans the erase,
   *        confirms, then runs programStateMachine() under a progress dialog
   *        and reports the result.
   * @details Refuses an image whose erase cannot be planned or that is for
   *          the other processor family than the attached tag. Saves the image
   *          as `lastImage` once confirmed.
   */
  void on_programButton_clicked();    // program tag
  /// Chooses the firmware ELF, starting in the last image's directory.
  void on_fileSelectButton_clicked(); // select file to program
  /// Chooses STM32_Programmer_CLI and saves it as the `programmer` setting.
  void on_toolSelectButton_clicked(); // select programming tool
  /// Connect: looks for a base if none is selected, then attaches.
  void on_connectButton_clicked();
  /// Disconnect: detaches.
  void on_disconnectButton_clicked();


  // Programming helpers

  /**
   * @brief One step of the program sequence; reschedules itself every second
   *        until FINISHED or FAILED.
   *
   * @details READY stops the tag unless it is IDLE, ABORTED, FINISHED, TEST
   *          or sRESET. STOPPING waits (up to 30 steps) and then, from
   *          ABORTED or FINISHED, has the tag's own firmware erase its log.
   *          ERASING waits for IDLE and detaches. PROGRAMMING runs flashTag().
   *          CONNECTING attaches to the new image (up to 5 tries). TESTING
   *          sets the clock, runs the self-tests, then starts the tag and goes
   *          round again with @p erase set, so the new firmware stops and
   *          erases itself and lays out its own storage. Any failure sets
   *          failure_ and FAILED.
   *
   * @param[in] erase  Stop and erase only, then FINISHED; no programming. Set
   *                   for the second pass after testing.
   */
  void programStateMachine(bool erase);
  /**
   * @brief Looks for USB tag bases and selects one, asking when there are
   *        several. With none, the window stays open and says so; Connect
   *        looks again.
   * @return true when a base is selected.
   */
  bool findBase();
  /**
   * @brief Finds STM32_Programmer_CLI: the location saved from an earlier
   *        choice, then STM32CUBEPROGRAMMER (the tool's path),
   *        STM32_PRG_PATH (its directory), the PATH, and the usual install
   *        directories, as embedded/tools/flash_release.py does.
   * @return The tool's path, or an empty string when none was found.
   */
  QString findProgrammer() const;
  /// Shows @p path as the image to program, enabling Program when it exists.
  void setImage(const QString &path);
  /**
   * @brief Asks the user to confirm, naming the tag, the image, the erase
   *        and what is lost and kept.
   * @return true to program.
   */
  bool confirmProgram(const FlashPlan &plan);
  /// Appends the programmer's standard output and error to the log pane.
  void processOutput();
  /**
   * @brief Runs STM32_Programmer_CLI once: plan_'s erase, then the image,
   *        then start it (`-g 0x08000000`), over SWD.
   * @return 0 on success; the tool's exit code, or 1 if it crashed.
   * @warning Blocks the GUI thread until the tool exits.
   */
  int flashTag();

private:

  Tag tag;                    ///< Connection to the tag through the selected base.
  Ui::MainWindow ui;          ///< The Designer-built widgets (mainwindow.ui).
  QTimer timer;               ///< Status poll, every 400 ms while attached.
  TagState current_state = STATE_UNSPECIFIED;  ///< Tag state at the last poll.
  ProgrammingState programming_state = ProgrammingState::READY;  ///< Current step of programStateMachine().
  /// Host version for the tag's qtmonitor_min_version check, which is disabled.
  const float version = 2.0;
  int external_flash_size = 0;  ///< Bytes, from TagInfo; sizes the erase progress bar.
  int sector_size = 4096;       ///< External flash erase sector, bytes.
  TagType tag_type = TAG_UNSPECIFIED;  ///< Attached tag's type, from TagInfo.
  UsbDev usbdev;              ///< The selected base; valid when have_base_.
  QProcess *process;          ///< The running programmer, during flashTag() only.
  QString program;            ///< Path of STM32_Programmer_CLI; empty if not found.
  FlashPlan plan_;            ///< Erase for the program in progress.
  QString failure_;           ///< Why the last program failed, for the user.
  int wait_ticks_ = 0;        ///< State-machine ticks spent in the current wait.
  QString capture_confirm_path_; ///< When set, confirmProgram() saves itself here and cancels.
  bool have_base_ = false;    ///< A USB base was found and selected (usbdev).
  /**
   * @brief Number of external-flash sectors the tag's erase will report, for
   *        the progress bar.
   * @return The tag's own total when it reports one
   *         (erase_sectors_total_plus_one); otherwise an estimate from the
   *         IMUTag page count or the external flash size.
   */
  int eraseSectorMaximum(const Status &status) const;
};

#endif // MAINWINDOW_H
