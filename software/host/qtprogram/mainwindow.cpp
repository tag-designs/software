
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
#include "custommessagebox.h"


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

  if (program.isEmpty())
    program = "Set Environment Variable STM32CUBEPROGAMMER to programming tool";

  ui.programmingTool->setText(program);

  //ui.programButton->setEnabled(true);
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

    if (0) { //min_version > version) {
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
    return true;
  }
  qDebug() << "Attach failed";
 return false;
}

void MainWindow::Detach()
{
  timer.stop();
  tag.Detach();
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
  
      if (status.state() == sRESET)
      {
        emit SectorsErased(status.sectors_erased());
      } 

      current_state = status.state();
    }
  }
  //emit StateUpdate(current_state);
}

int MainWindow::flashTag(){
  process = new QProcess(this);  // create on the heap, so it doesn't go out of scope
  QProcess::ExitStatus status;
  int ret;

  // program arguments

  QStringList args;
  args << "-c" << "port=SWD" << "mode=UR" << "-d" << ui.fileName->text() << "-q" << "-g" << "0x08000000";

  // connect process output 

  connect (process, SIGNAL(readyReadStandardOutput()), this, SLOT(processOutput())); 
  connect (process, SIGNAL(readyReadStandardError()), this, SLOT(processOutput()));  

  // execute programmer
  process->start(program, args);  // start the process
  process->waitForStarted();
  qDebug() << "programmer started";
  process->waitForFinished();
  status = process->exitStatus();
  qDebug() << "programmer finished";
  qDebug() << "Exit status " << status;
  ret = process->exitCode();
  delete process;
  process = nullptr;
  if (status == QProcess::NormalExit)
    return ret;
  else  
    return 1;
}

void MainWindow::programStateMachine()
{
  bool attached = tag.IsAttached();
  switch (programming_state) {
     case ProgrammingState::READY:
        qDebug() << "Ready";
        programming_state = ProgrammingState::STOPPING;
        if (attached && ((current_state == RUNNING) || (current_state == HIBERNATING))) 
        { 
          qDebug() << "tag.IsAttached() " << tag.IsAttached() << " tag.Stop() "  << tag.Stop();
        }
        qDebug() << "Stopping";
        break;
      case ProgrammingState::STOPPING:
        
        if (attached && ((current_state == RUNNING) || (current_state == HIBERNATING)))
        {
          break;
        }
        programming_state = ProgrammingState::ERASING;
        if (attached && ((current_state == ABORTED) || (current_state == FINISHED)))
        {
          qDebug() << "Erasing";
          if (!tag.Erase())
          {
            qDebug() << "tag reset returned false";
            return;  // exit state machine
          } 
          break;
        }     
      case ProgrammingState::ERASING:
        if (attached && ((current_state == sRESET) || (current_state == ABORTED) || (current_state == FINISHED)))
          break;
        if (attached){
          Detach();
        }
        programming_state = ProgrammingState::PROGRAMMING;
        qDebug() << "Programming";
      case ProgrammingState::PROGRAMMING:
        if (flashTag()) {
          programming_state = ProgrammingState::FAILED;
          break;
        }
        Attach();
        
        if (attached) 
        {
          qDebug() << "Testing";
          tag.SetRtc();
          tag.Test(RUN_ALL);
          programming_state = ProgrammingState::TESTING;
          break;
        } 
        else 
        {
          programming_state = ProgrammingState::FAILED;
          break;
        }
      case ProgrammingState::TESTING:
        if (attached && (current_state == TEST))
        {
          if (current_state == TEST)
            break;
          // reset clock
          tag.SetRtc();
        }
        qDebug() << "Tests Complete";
        programming_state = ProgrammingState::FINISHED;
      case ProgrammingState::FINISHED:
        qDebug() << "Finished";
        Detach();
        break;
      case ProgrammingState::FAILED:
        break;
  }

  emit ProgrammingStateUpdate(programming_state);
  if ((programming_state != ProgrammingState::FINISHED) && (programming_state != ProgrammingState::FAILED))
     QTimer::singleShot(1000, this, &MainWindow::programStateMachine);
}


void MainWindow::on_programButton_clicked(){
  programming_state = ProgrammingState::READY;
  if (!tag.IsAttached())
    Attach();
  //int ret;

  // check if programming tool exists
  if (program.isEmpty() || !QFile::exists(program)){
    QMessageBox msgBox;
    msgBox.setText("Programming Tool not found");
    msgBox.exec();
    qDebug() << "no program: " << program;
    return;
  } 

  CustomMessageBox msgBox = CustomMessageBox("Programming Tag",this);

  // connect signals

  if (tag.IsAttached()) {
    msgBox.setMaximum(external_flash_size/sector_size);
    connect(this,&MainWindow::SectorsErased,&msgBox,&CustomMessageBox::setValue);
  }

  connect(&msgBox, &CustomMessageBox::OkButtonClicked, 
          this, &MainWindow::programStateMachine);

  connect(this,&MainWindow::ProgrammingStateUpdate, this, [&msgBox](ProgrammingState newstate){
     switch (newstate) {
        case ProgrammingState::READY:
          msgBox.setText("Ready");
          break;
        case ProgrammingState::STOPPING:
          msgBox.setText("Stopping");
          break;
        case ProgrammingState::ERASING:
          msgBox.setText("Erasing");
          break;
        case ProgrammingState::PROGRAMMING:
          msgBox.setText("Programming");
          break;
        case ProgrammingState::TESTING:
          msgBox.setText("Testing");
          break;
        case ProgrammingState::FINISHED:
        case ProgrammingState::FAILED:
          msgBox.setText("Finished");
          msgBox.done( QDialog::Accepted);
          break;
    }
  });

  msgBox.exec();
  if (programming_state == ProgrammingState::FAILED) {
    QMessageBox msgBox;
    msgBox.setText("Programming Failed");
    msgBox.exec();
  } else {
    if (ui.info_testStatus->text() != "ALL_PASSED"){
      QMessageBox msgBox;
      msgBox.setText("Test Failed");
      msgBox.exec();
    }
  }
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
    ui.programButton->setEnabled(false);
  }
}

void MainWindow::on_toolSelectButton_clicked()
{
 
  QString filename = QFileDialog::getOpenFileName(this,tr("STM32 Programming CLI Tool"), QDir::homePath());
  if (!filename.isNull()) {
    ui.programmingTool->setText(filename);
  }
}

//void MainWindow::lBar(int currProgress, int total){}
//void MainWindow::DisplayMessage(int msgType, QString str){}
//void MainWindow::logMessage(int msgType, QString str){}

void MainWindow::processOutput(){
  QString output = QString::fromLocal8Bit(process->readAllStandardOutput());
  output.append(QString::fromLocal8Bit(process->readAllStandardError()));
  QStringList lines = output.split('\n', Qt::SkipEmptyParts);
  s_textEdit->append(lines.join('\n'));
  //qInfo().noquote() << output;;  / read error channel 
}
