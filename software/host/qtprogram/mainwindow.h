#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include "tagclass.h"
#include "ui_mainwindow.h"

class QWidget;
class QTextEdit;

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

  void lBar(int currProgress, int total);
  void DisplayMessage(int msgType, QString str);
  void logMessage(int msgType, QString str);


private slots:

  // attach/detach to tag

  bool Attach();

  // timer tick

  void TriggerUpdate();

  // control tab events

  void on_programButton_clicked();    // program tag
  void on_fileSelectButton_clicked(); // select file to program

  // data events

  //void on_tagLogSaveButton_clicked(); // download data from tag

private:
  Tag tag;
  Ui::MainWindow ui;
  QTimer timer;
  TagState current_state = STATE_UNSPECIFIED;
  const float version = 2.0;
};

#endif // MAINWINDOW_H
