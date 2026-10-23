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

extern QTextEdit *s_textEdit;
class LogScreen;

 enum class ProgrammingState {
     READY,
     STOPPING,
     ERASING,
     PROGRAMMING,
     CONNECTING,   ///< Waiting for the newly programmed tag to answer.
     TESTING,
     FINISHED,
     FAILED
  };

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  explicit MainWindow(QWidget *parent = nullptr);
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

  void StateUpdate(TagState state);
  void ProgrammingStateUpdate(ProgrammingState);

#ifndef __arm64__
  void lBar(int currProgress, int total);
  void DisplayMessage(int msgType, QString str);
  void logMessage(int msgType, QString str);
#endif

  void SectorsErased(int);
  void IdleState(void);


private slots:

  // attach/detach to tag

  bool Attach();
  void Detach();

  // timer tick

  void TriggerUpdate();

  // control tab events

  void on_programButton_clicked();    // program tag
  void on_fileSelectButton_clicked(); // select file to program
  void on_toolSelectButton_clicked(); // select programming tool
  void on_connectButton_clicked();
  void on_disconnectButton_clicked();


  // Programming helpers

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
  void processOutput();
  int flashTag();

private:

  Tag tag;
  Ui::MainWindow ui;
  QTimer timer;
  TagState current_state = STATE_UNSPECIFIED;
  ProgrammingState programming_state = ProgrammingState::READY;
  const float version = 2.0;
  int external_flash_size = 0;
  int sector_size = 4096;
  TagType tag_type = TAG_UNSPECIFIED;
  UsbDev usbdev;
  QProcess *process;
  QString program;
  FlashPlan plan_;            ///< Erase for the program in progress.
  QString failure_;           ///< Why the last program failed, for the user.
  int wait_ticks_ = 0;
  QString capture_confirm_path_; ///< When set, confirmProgram() saves itself here and cancels.
  bool have_base_ = false;    ///< A USB base was found and selected (usbdev).        ///< State-machine ticks spent in the current wait.
  int eraseSectorMaximum(const Status &status) const;
};

#endif // MAINWINDOW_H
