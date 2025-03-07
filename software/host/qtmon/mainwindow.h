#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include "tagclass.h"
#include "schedule.h"
#include "ui_mainwindow.h"

class QWidget;
class QTextEdit;
class QGroupBox;
class ConfigTab;



extern QTextEdit *s_textEdit;
class LogScreen;

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

private slots:

  // attach/detach to tag

  bool Attach();

  // timer tick

  void TriggerUpdate();

  // control tab events

  void on_syncButton_clicked();     // synchronize clock
  void on_stopButton_clicked();     // stop tag
 
  void on_eraseButton_clicked();    // reset tag flash
  void on_testButton_clicked();     // run tag self-test

  void on_Attach_clicked();         // attach to tag
  void on_Detach_clicked();         // detach from tag

  // data events

  void on_tagLogSaveButton_clicked(); // download data from tag

  

private:
  Tag tag;
  Ui::MainWindow ui;
  QTimer timer;
  TagState current_state = STATE_UNSPECIFIED;
  const float version = 2.0;
};

#endif // MAINWINDOW_H
