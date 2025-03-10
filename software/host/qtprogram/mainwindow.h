#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include <QProcess>
#include "tagclass.h"
#include "ui_mainwindow.h"

class QWidget;
class QTextEdit;

extern QTextEdit *s_textEdit;
class LogScreen;

 enum class ProgrammingState {
     READY,
     STOPPING,
     ERASING,
     PROGRAMMING,
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

  // Programming helpers

  void programStateMachine();
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
  UsbDev usbdev;
  QProcess *process;
  QString program;
};

#endif // MAINWINDOW_H
