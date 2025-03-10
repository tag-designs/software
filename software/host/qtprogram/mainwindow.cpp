
#include <QMainWindow>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTime>
#include <QTimer>
#include <QLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QProcessEnvironment>

#include <QFuture>

#include <ctime>
#include <fstream>
#include <iomanip>

#include <google/protobuf/util/json_util.h>
#include <streambuf>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "tag.pb.h"

#include "download.h"


#ifndef __arm64__
#include <DisplayManager.h>
#include <CubeProgrammer_API.h>
#endif

#define title_string "Tag Programmer v1.0"


// main window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  setWindowTitle(QString(title_string));

  // Find base

  std::vector<UsbDev> usbdevs;
  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("No Tag Bases Found");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    QTimer::singleShot(0, this, SLOT(close()));;
  }

  int index = 0;
  if (usbdevs.size() > 1)
  {
    QStringList items;

    for (int j = 0; j < usbdevs.size(); j++)
    {
      QString bus = QString::number(usbdevs[j].bus).rightJustified(3, '0');
      QString address = QString::number(usbdevs[j].address).rightJustified(3, '0');
      QString vid = QString::number(usbdevs[j].vid, 16); //.rightJustified(4,'0');
      QString pid = QString::number(usbdevs[j].pid, 16); //.rightJustified(4,'0');
      QString s = QString("%1:%2    0x%3:0x%4").arg(bus, address, vid, pid);
      items << s;
    }

    QInputDialog *inputDialog = new QInputDialog();
    inputDialog->setOption(QInputDialog::UseListViewForComboBoxItems);
    inputDialog->setComboBoxItems(items);
    inputDialog->setWindowTitle("Available Bases");
    inputDialog->setLabelText("Please select a base");
    if (!inputDialog->exec())
      QTimer::singleShot(0, this, SLOT(close()));;
    QString item = inputDialog->textValue();
    if (!item.isEmpty())
    {
      index = items.indexOf(item);
    }
    if (index > usbdevs.size())
      index = 0;

    usbdev = usbdevs[index];
  }

  // Tag state poll timer

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // attach if possible to collect tag information

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  program = env.value("STM32CUBEPROGRAMMER");

  ui.programButton->setEnabled(true);
  if (Attach())
    tag.Detach();
}

MainWindow::~MainWindow()
{
  tag.Detach();
}

// attach to tag

bool MainWindow::Attach()
{
  if (tag.IsAttached())
  {
    qDebug() << "Already attached to tag";
    return false;
  }

  if (tag.Attach(usbdev))
  {
    std::string str;
    int size;
    Config config;
    Status status;
    TagInfo info;
    tag.GetTagInfo(info);
    //tag.GetConfig(config);
    tag.GetStatus(status);

    // check qtmonitor version 

    float min_version = info.qtmonitor_min_version();

    if (min_version > version) {
      tag.Detach();
      return false;
    }

   // fill information table

    ui.info_tagtype->setText(
        QString::fromStdString(TagType_Name(info.tag_type())));
    ui.info_firmware->setText(QString::fromStdString(info.firmware()));
    ui.info_gitHash->setText(QString::fromStdString(info.githash()));
    ui.info_uuid->setText(QString::fromStdString(info.uuid()));
    ui.info_buildDate->setText(QString::fromStdString(info.build_time()));
    external_flash_size=info.extflashsz();
    // start the StateUpdate timer
    
    TriggerUpdate();
    timer.start(400);
    attached = true;
    return true;
  }
 return false;
}

void MainWindow::Detach()
{
  timer.stop();
  tag.Detach();
  attached = false;
}

// While tag is attached, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{

  Status status;

  if (tag.IsAttached())
  {
    std::string msg;

    float voltage;
    if (tag.Voltage(voltage))
    {
      ui.info_Voltage->setText(
          QString::number(static_cast<double>(voltage), 'f', 2));
    }

    // update status data

    if (tag.GetStatus(status))
    {
      ui.State->setText(QString::fromStdString(TagState_Name(status.state())));
  
      double timeerr = QDateTime::currentMSecsSinceEpoch();
      timeerr = status.millis() - timeerr;
      ui.timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
      ui.info_testStatus->setText(QString::fromStdString(TestResult_Name(status.test_status())));

      if (status.state() == IDLE)
      {
         emit IdleState();
      }
      else
      {
        if (status.state() == sRESET)
        {
          emit SectorsErased(status.sectors_erased());
        } 

        if (status.state() != current_state)
        {
          if ((status.state() == RUNNING) || (status.state() == CONFIGURED) ||
              (status.state() == HIBERNATING))
          {
           
          }
          else
          {
        
          }

          if ((status.state() == ABORTED) ||
              (status.state() == FINISHED))
          {
            //ui.tagLogSaveButton->setEnabled(true);
          }
          else
          {
              //ui.tagLogSaveButton->setEnabled(false);
          }
        
        }
      }
      current_state = status.state();
     
    }
  }
  //emit StateUpdate(current_state);
}

void MainWindow::programStateMachine(){
  switch (programming_state) {
     case ProgrammingState::READY:
        qDebug() << "Ready";
        programming_state = ProgrammingState::STOPPING;
        if (attached && ((current_state == RUNNING) || (current_state == HIBERNATING))) 
        { 
          tag.Stop();
          break;
        }
      case ProgrammingState::STOPPING:
         qDebug() << "Stopping";
        if (attached && ((current_state == RUNNING) || (current_state == HIBERNATING)))
        {
          break;
        }
        programming_state = ProgrammingState::ERASING;
        if (attached && ((current_state == ABORTED) || (current_state == FINISHED)))
        {
          if (!tag.Erase())
          {
            qDebug() << "tag reset returned false";
          } else {
            if (external_flash_size) 
            {
                QProgressDialog progress("Erasing flash...", "exit dialog", 0, external_flash_size/sector_size, this);
                progress.setWindowModality(Qt::WindowModal);
                connect(this,SIGNAL(SectorsErased(int)),&progress,SLOT(setValue(int)));
                connect(this,SIGNAL(IdleState()), &progress, SLOT(close()));
                progress.exec();
            } else {
                QThread::msleep(100);
                TriggerUpdate();
            }
          };
        }    
      case ProgrammingState::ERASING:
        qDebug() << "Erasing";
        
        programming_state = ProgrammingState::PROGRAMMING;
        break;
      case ProgrammingState::PROGRAMMING:
        qDebug() << "Programming";
       
        Detach();
        programming_state = ProgrammingState::TESTING;
        break;
      case ProgrammingState::TESTING:
        qDebug() << "Testing";
        
        Attach();
        programming_state = ProgrammingState::FINISHED;
        break;
      case ProgrammingState::FINISHED:
        qDebug() << "Finished";
        
        break;
  }

  emit ProgrammingStateUpdate(programming_state);
  if (programming_state != ProgrammingState::FINISHED)
     QTimer::singleShot(1000, this, &MainWindow::programStateMachine);
}


void MainWindow::on_programButton_clicked(){
  qDebug() << "run program";
  const QString messages[] = {"Ready","Stopping","Erasing","Programming","Testing","Finished"};
  programming_state = ProgrammingState::READY;
  Attach();
  int ret;
  QMessageBox msgBox;
  msgBox.setStandardButtons(QMessageBox::Cancel);
  QPushButton continueButton = QPushButton("Continue");
  msgBox.addButton(&continueButton,QMessageBox::NoRole);
  continueButton.disconnect();

  // connect signals

   connect(&continueButton, &QAbstractButton::clicked, this, [&](){
          for (QAbstractButton *btn : msgBox.buttons()){
            msgBox.removeButton(btn);
          }
          qDebug() << "Calling State Machine";
          programStateMachine();
  });

/*
  connect(&msgBox, &QMessageBox::buttonClicked, this, [&](QAbstractButton *button){
        if (msgBox.standardButton(button) == QMessageBox::Cancel){
          qDebug() << "Cancel button clicked";
          msgBox.close();
        } else {
          for (QAbstractButton *btn : msgBox.buttons()){
            msgBox.removeButton(btn);
          }
          programStateMachine();
        }
  });

  */

  /*

  connect(this,&MainWindow::ProgrammingStateUpdate, this, [&msgBox,&messages](ProgrammingState newstate){
     //msgBox.setText(messages[(unsigned long) newstate]);
     if (newstate == ProgrammingState::FINISHED){
      msgBox.done( QDialog::Accepted);
     }
  });
  */

  msgBox.exec();
  





  /*
  if (current_state != IDLE){ 
      QMessageBox msgBox;
      msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Cancel);
      if (current_state == ABORTED || current_state == FINISHED){
        msgBox.setWindowTitle("Tag state is not erased");
        msgBox.setText("Erase tag data ?");
        ret = msgBox.exec();
      } else {
        msgBox.setWindowTitle("Tag is running");
        msgBox.setText("Stop tag and erase data ?");
        ret = msgBox.exec();
        if (ret == QMessageBox::Ok){
          tag.Stop();
        } else {
          Detach();
          return;
        }
      }
      if (ret == QMessageBox::Ok)
      {
        
      } else {
        Detach();
        return;
      }
  }
  Detach();
        
  if (program.isEmpty()){
    qDebug() << "no program";
  } else {
    process = new QProcess(this);  // create on the heap, so it doesn't go out of scope
    QStringList args;
    args << "-c" << "port=SWD" << "mode=UR" << "-d" << ui.fileName->text() << "-g" << "0x08000000";
    connect (process, SIGNAL(readyReadStandardOutput()), this, SLOT(processOutput()));  // connect process signals with your code
    connect (process, SIGNAL(readyReadStandardError()), this, SLOT(processOutput()));  // same here
    process->start(program, args);  // start the process
    process->waitForStarted();
    qDebug() << "programmer started";
    process->waitForFinished();
    qDebug() << "programmer finished";
    qDebug() << "Exit status " << process->exitStatus();
    
  }
  */
}

void MainWindow::on_fileSelectButton_clicked()
{
 
  QString filename = QFileDialog::getOpenFileName(this,tr("binary file to program"), 
                                                 QDir::homePath(),
                                                 tr("Binary (*.bin *.elf)"));
  if (!filename.isNull()) {
    ui.fileName->setText(filename);
    ui.programButton->setEnabled(true);
  } else {
    //ui.programButton->setEnabled(false);
  }
}

//void MainWindow::lBar(int currProgress, int total){}
//void MainWindow::DisplayMessage(int msgType, QString str){}
//void MainWindow::logMessage(int msgType, QString str){}

void MainWindow::processOutput(){
  qInfo() << process->readAllStandardOutput();  // read normal output
  qInfo() << process->readAllStandardError();  // read error channel 

}