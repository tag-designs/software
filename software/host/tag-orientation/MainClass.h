#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QDebug>
#include <QTimer>


#include "tagclass.h"
#include "tag.pb.h"
#include "compassdata.h"

class MainClass : public QObject
{
    Q_OBJECT

public:
    MainClass(QObject *parent = nullptr);
    ~MainClass();

public slots:
    void closeApp();

private slots:

  bool Attach();
  void Detach();
  bool load_calibration();

  // timer tick

  void TriggerUpdate();

private:

  CompassData magnetic;
  Tag tag;
  Config config;
  TagInfo info;
  QTimer timer;
  UsbDev usbdev;
  bool isStreaming = false;


};