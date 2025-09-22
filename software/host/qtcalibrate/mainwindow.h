#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include <QProcess>
#include <QRandomGenerator>
#include "tagclass.h"
#include "ui_mainwindow.h"

#include "tag.pb.h"



class QWidget;
class QTextEdit;

extern int log_level;

class LogScreen;


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
 

signals:

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

  void on_connectButton_clicked();
  void on_disconnectButton_clicked();

  void on_logsaveButton_clicked();
  void on_logclearButton_clicked();
  void on_loglevelBox_currentIndexChanged(int index);
  void on_addButton_clicked();
  void on_clearButton_clicked();


  // Programming helpers


private:

  void logWindowInit(void);
  void scatterGraphInit(void);
  QRandomGenerator *generator;

  Tag tag;
  Config config;
  TagInfo info;
  Ui::MainWindow ui;
  QTimer timer;
  UsbDev usbdev;


};

#endif // MAINWINDOW_H
