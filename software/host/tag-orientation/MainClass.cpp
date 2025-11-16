#include <QTimer>
#include <QDebug>
#include <QTIME>
#include <QTIMER>
#include <QVector3D>

#include "MainClass.h"

#include <ctime>


MainClass::MainClass(QObject *parent) : QObject(parent)
{
   // QTimer::singleShot(3000, this, &MainClass::closeApp); //if you need process some code in closeApp()
    //QTimer::singleShot(1000, QCoreApplication::instance(), SLOT(quit())); //if you need quit() only
    connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));
    if (Attach()) {
        if (load_calibration()){
            tag.SetRtc();
            tag.Calibrate();
            timer.start(200);
            isStreaming = true;
        }
    }
}

MainClass::~MainClass()
{
    tag.Detach();
   // delete this; //not need because rial has a parent, you can check it using  qDebug() << "par=" << s.parent();
}

void MainClass::closeApp(){
    Detach();
    qDebug() << "good bye";
    QCoreApplication::quit();
}

// attach to tag

bool MainClass::Attach()
{
  if (tag.IsAttached())
  {
    qInfo() << "Already attached to tag";
    return false;
  }

  // Find base

  std::vector<UsbDev> usbdevs;

  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    qInfo() << "No Tag Bases Found";
    return false;
  }

  if (usbdevs.size() > 1)
  {
    qInfo() << "More than one tag base found, selection first";
  }

  usbdev = usbdevs[0];

  if (tag.Attach(usbdev))
  {
    std::string str;
    int size;
    Status status;
    //Info info;
    tag.GetTagInfo(info);

    tag.GetConfig(config);
    tag.GetStatus(status);

    if (status.state() != IDLE) 
    {
        qInfo() << "Tag not in idle state";
        Detach();
        return false;
    }
    qInfo() << "Attach succeeded";
    return true;
  }
  qInfo() << "Attach failed";
  return false;
}

// Detach from tag

void MainClass::Detach()
{
  timer.stop();
  tag.Detach();
  isStreaming = false;
}

bool MainClass::load_calibration(){
   Ack ack;
   float B;
   float V[3];
   float A[3][3];

   if (tag.ReadCalibration(ack,-1)
        && ack.has_calibration_constants() 
        && ack.calibration_constants().has_magnetometer())
   {
      const CalibrationConstants_MagConstants mag = ack.calibration_constants().magnetometer();
      B = mag.b();
      V[0] = mag.v0();
      V[1] = mag.v1();
      V[2] = mag.v2();
      A[0][0] = mag.a00();
      A[0][1] = mag.a01();
      A[0][2] = mag.a02();
      A[1][0] = mag.a10();
      A[1][1] = mag.a11();
      A[1][2] = mag.a12();
      A[2][0] = mag.a20();
      A[2][1] = mag.a21();
      A[2][2] = mag.a22();
      magnetic.setCalibrationConstants(B,V,A);

      qInfo() << "Read timestamp " << ack.calibration_constants().timestamp();
      return true;
     
   } else {
      qInfo() << "Read calibration failed";
      return false;
   }
}


void MainClass::TriggerUpdate(void)
{
  Status status;
  Ack ack;
  static bool done;
  QVector3D mag, accel;
  float mx,my,mz,ax,ay,az;
  float pitch, roll, yaw, dip, field;
  QQuaternion q;
  
  if (isStreaming)
  { 
    tag.GetCalibrationLog(ack);
    if (ack.has_calibration_log()) {
        for(auto const &sdata : ack.calibration_log().data())
        {
          if (sdata.has_mag()){
              mx = sdata.mag().mx();
              my = sdata.mag().my();
              mz = sdata.mag().mz();
              mag = QVector3D(mx,my,mz);
          } else {
            continue;
          }

          if (sdata.has_accel()){
            ax = sdata.accel().ax();
            ay = sdata.accel().ay();
            az = sdata.accel().az();
            accel = QVector3D(ax,ay,az);
            if (magnetic.eCompass(mag, accel, q, dip, field)) {
              // display angles -- note that QT getEulerAngles uses a strange convention
              // pitch around x axis, yaw around y axis, and roll around z axis.  Order is YXZ for Q->angles, ZXY for angles->Q
              // we want x:pitch,y:roll,z:yaw
              // note that pitch and roll both have signs reversed.

              q.getEulerAngles(&pitch,&roll,&yaw);
              if (yaw < 0.0) yaw += 360.0;
              qInfo() << "Pitch: " << pitch << " Roll " << roll << " Yaw " << yaw;
            }
         }
        }   
    } 
    }
}