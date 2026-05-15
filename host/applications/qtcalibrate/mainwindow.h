#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "tagclass.h"
#include "ui_mainwindow.h"

#include "attitude_display.h"
#include "compass_display.h"
#include "tag.pb.h"
#include "compassdata.h"



class QAction;
class QMenu;

extern int log_level;

// MainWindow owns the live application workflow: USB/tag attachment,
// calibration controls, log display, and the embedded shared sensorui QML
// widgets used to preview compass and attitude.
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
  void TriggerQualityUpdate();

  // control tab events

  void on_attachButton_clicked();
  void on_detachButton_clicked();
  void on_streamCheckBox_toggled(bool);

  // calibration 


  void on_startButton_clicked();
  void on_stopButton_clicked();
  void on_clearButton_clicked();
  void calibration_update(void);
  void on_saveButton_clicked();
  void on_loadButton_clicked();

  // log and orientation display actions

  void on_logsaveButton_clicked();
  void on_logclearButton_clicked();
  void on_loglevelBox_currentIndexChanged(int index);
  void setDeclination();
  void batteryForwardToggled(bool checked);
  void showOrientationContextMenu(const QPoint &pos);



private:

  void logWindowInit(void);
  void scatterGraphInit(void);
  void orientationControlsInit(void);
  void updateDeclinationActionText();
  // These methods bridge live orientation results into the shared sensorui
  // facades. They intentionally avoid direct QML method calls in MainWindow.
  void rotateImage(QQuaternion qt);
  void setOrientation(float h, float p, float r, float d, float f, float g);

  // CompassData owns the inherited calibration solver state. MainWindow owns
  // the tag connection and decides when to feed samples into that state.
  CompassData magnetic;
  CompassDisplay compassDisplay;
  AttitudeDisplay attitudeDisplay;
  Tag tag;
  Config config;
  TagInfo info;
  Ui::MainWindow ui;
  QTimer timer;
  QTimer qualitytimer;
  UsbDev usbdev;
  QMenu *configurationMenu = nullptr;
  QAction *declinationAction = nullptr;
  QAction *batteryForwardAction = nullptr;
  double declinationDegrees = 0.0;
  bool batteryForward = true;

  bool isCalibrating = false;
  bool isOrienting = false;
  bool isStreaming = false;


};

#endif // MAINWINDOW_H
