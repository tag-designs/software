
#include <QMainWindow>
#include <QDate>
#include "qtuserguide.h"
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
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QPushButton>
#include <algorithm>

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

#include "custommessagebox.h"
#include "qtfiledialog.h"

namespace
{

// Fallback for older IMUTag firmware that predates
// Status.erase_sectors_total_plus_one. New firmware reports the erase total
// directly, so the UI does not need tag-specific page geometry.
constexpr int kImuDataLogPageBytes = 2048;

int roundedSectorCount(qint64 bytes, int sector_size)
{
  if (bytes <= 0 || sector_size <= 0)
    return 0;
  return static_cast<int>((bytes + sector_size - 1) / sector_size);
}

} // namespace

#define title_string "Tag Programmer v1.1"


// main window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  setWindowTitle(QString(title_string));
  HostUserGuide::addHelpMenu(
      this, tr("Tag Programmer"), QStringLiteral("qtprogram"),
      tr("%1\n\nErase, program and test a tag's firmware through a "
         "programmer or USB base.").arg(title_string));

  // Find base. With none, the window still opens, so it can be looked at
  // and its programmer and image set; Connect looks again.

  findBase();

  // Tag state poll timer

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // attach if possible to collect tag information

  program = findProgrammer();
  ui.programmingTool->setText(program);
  ui.programmingTool->setPlaceholderText(
      tr("STM32_Programmer_CLI not found: choose it with the button"));
  ui.fileName->setPlaceholderText(tr("Choose the firmware .elf to program"));
  setImage(QSettings().value("lastImage").toString());

  if (have_base_ && Attach())
    tag.Detach();
}

MainWindow::~MainWindow()
{
  tag.Detach();
}

bool MainWindow::findBase()
{
  std::vector<UsbDev> usbdevs;
  have_base_ = false;
  if (!tag.Available(usbdevs) || usbdevs.empty()) {
    ui.statusBar->showMessage(tr("No tag base found. Connect one and press Connect."));
    qInfo() << "No tag base found";
    return false;
  }

  size_t index = 0;
  if (usbdevs.size() > 1) {
    QStringList items;
    for (const UsbDev &dev : usbdevs) {
      QString bus = QString::number(dev.bus).rightJustified(3, '0');
      QString address = QString::number(dev.address).rightJustified(3, '0');
      items << QString("%1:%2    0x%3:0x%4")
                   .arg(bus, address, QString::number(dev.vid, 16),
                        QString::number(dev.pid, 16));
    }
    bool ok = false;
    const QString item = QInputDialog::getItem(this, tr("Available Bases"),
                                               tr("Please select a base"), items, 0,
                                               false, &ok);
    if (!ok)
      return false;
    index = std::max<qsizetype>(0, items.indexOf(item));
  }
  usbdev = usbdevs[index];
  have_base_ = true;
  ui.statusBar->clearMessage();
  return true;
}

QString MainWindow::findProgrammer() const
{
  auto usable = [](const QString &path) {
    const QFileInfo info(path);
    return !path.isEmpty() && info.isFile() && info.isExecutable();
  };
  const QString saved = QSettings().value("programmer").toString();
  if (usable(saved))
    return saved;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (usable(env.value("STM32CUBEPROGRAMMER")))
    return env.value("STM32CUBEPROGRAMMER");
#ifdef Q_OS_WIN
  const QString cli = QStringLiteral("STM32_Programmer_CLI.exe");
#else
  const QString cli = QStringLiteral("STM32_Programmer_CLI");
#endif
  if (env.contains("STM32_PRG_PATH") && usable(QDir(env.value("STM32_PRG_PATH")).filePath(cli)))
    return QDir(env.value("STM32_PRG_PATH")).filePath(cli);
  const QString onPath = QStandardPaths::findExecutable(cli);
  if (!onPath.isEmpty())
    return onPath;
  QStringList roots;
#if defined(Q_OS_MACOS)
  const QString bundle =
      "STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin";
  roots << QDir::home().filePath("Software/" + bundle)
        << "/Applications/STMicroelectronics/" + bundle;
#elif defined(Q_OS_WIN)
  roots << "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin";
#else
  roots << QDir::home().filePath("Software/STM32CubeProgrammer/bin")
        << "/opt/st/stm32cubeprog/bin";
#endif
  for (const QString &root : roots)
    if (usable(QDir(root).filePath(cli)))
      return QDir(root).filePath(cli);
  return QString();
}

void MainWindow::setImage(const QString &path)
{
  const bool exists = !path.isEmpty() && QFileInfo::exists(path);
  ui.fileName->setText(exists ? path : QString());
  ui.programButton->setEnabled(exists);
}

bool MainWindow::confirmProgram(const FlashPlan &plan)
{
  const bool attached = tag.IsAttached();
  const bool calibrated = attached && (tag_type == COMPASSTAG || tag_type == IMUTAG);
  const QString bullet = QStringLiteral("\u2022 ");
  QStringList lost, kept;
  if (attached)
    lost << tr("The tag's log. Download it first with Tag Monitor.");
  lost << tr("The tag's stored configuration.");
  if (calibrated && !plan.keepsCalibration())
    lost << tr("Its calibration constants: this image keeps no calibration page. "
               "Is it the right image for this tag?");
  if (calibrated && plan.keepsCalibration())
    kept << tr("Its calibration constants.");
  kept << tr("The RTC trim and the board UUID.");

  QString text = attached
      ? tr("Erase and program %1 %2?")
            .arg(QString::fromStdString(std::string(TagType_Name(tag_type))),
                 ui.info_uuid->text())
      : tr("Program the tag on the programmer?");
  QString detail = tr("Image: %1\n%2\n\nLost:\n%3\n\nKept:\n%4")
                       .arg(QFileInfo(ui.fileName->text()).fileName(), plan.summary(),
                            bullet + lost.join("\n" + bullet),
                            bullet + kept.join("\n" + bullet));
  if (!attached)
    detail += tr("\n\nNo tag answers through a base, so its log cannot be "
                 "erased by its own firmware first.");
  if (plan.erase == FlashPlan::Erase::All)
    detail += tr("\n\nIf the tag does not answer afterwards, power-cycle it "
                 "and press Connect.");

  QMessageBox box(this);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(tr("Program Tag"));
  box.setText(text);
  box.setInformativeText(detail);
  QPushButton *go = box.addButton(tr("Erase and Program"), QMessageBox::AcceptRole);
  QPushButton *cancel = box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(cancel);
  box.setEscapeButton(cancel);
  box.exec();
  return box.clickedButton() == go;
}

int MainWindow::eraseSectorMaximum(const Status &status) const
{
  if (status.erase_sectors_total_plus_one() > 0)
    return status.erase_sectors_total_plus_one() - 1;

  if (tag_type == IMUTAG)
    return roundedSectorCount(
        static_cast<qint64>(status.external_data_count()) * kImuDataLogPageBytes,
        sector_size);

  return roundedSectorCount(external_flash_size, sector_size);
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

    if (!status.debug_message().empty()){
      qDebug() << "Log: " << QString::fromStdString(status.debug_message());
    }

    // check qtmonitor version 

    float min_version = info.qtmonitor_min_version();

    if (0) { //min_version > version) {
      tag.Detach();
      return false;
    }

   // fill information table

    ui.StatusGroup->setEnabled(true);
    ui.TagInformation->setEnabled(true);

    ui.info_tagtype->setText(
        QString::fromStdString(TagType_Name(info.tag_type())));
    ui.info_firmware->setText(QString::fromStdString(info.firmware()));
    ui.info_gitHash->setText(QString::fromStdString(info.githash()));
    ui.info_uuid->setText(QString::fromStdString(info.uuid()));
    ui.info_buildDate->setText(QString::fromStdString(info.build_time()));
    external_flash_size=info.extflashsz();
    tag_type = info.tag_type();
    // start the StateUpdate timer
    
    ui.connectButton->setEnabled(false);
    ui.disconnectButton->setEnabled(true);
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
  ui.connectButton->setEnabled(true);
  ui.disconnectButton->setEnabled(false);
  ui.StatusGroup->setEnabled(false);
  ui.TagInformation->setEnabled(false);
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

    if (!status.debug_message().empty()){
      qDebug() << "Log: " << QString::fromStdString(status.debug_message());
    }
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
  // Erase and program in one invocation: two leave a reset between them with
  // the flash empty, which latches FLASH_SR.PEMPTY (release procedure).
  args << "-c" << "port=SWD" << "mode=UR" << plan_.eraseArgs() << "-d"
       << ui.fileName->text() << "-q" << "-g" << "0x08000000";
  qInfo().noquote() << "Programming:" << program << args.join(' ');

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

void MainWindow::programStateMachine(bool erase_only)
{
  // Called once a second. A tag that is not IDLE is stopped if it is running
  // and then erased by its own firmware, which knows its log's layout, before
  // the new image is written.
  const bool attached = tag.IsAttached();
  const TagState state = current_state;
  auto fail = [this](const QString &why) {
    failure_ = why;
    qWarning().noquote() << "Programming failed:" << why;
    programming_state = ProgrammingState::FAILED;
  };
  Config config;
  switch (programming_state) {
     case ProgrammingState::READY:
        failure_.clear();
        wait_ticks_ = 0;
        programming_state = ProgrammingState::STOPPING;
        if (attached && state != IDLE && state != ABORTED && state != FINISHED &&
            state != TEST && state != sRESET) {
          qInfo() << "Stopping the tag, which is" << TagState_Name(state).c_str();
          tag.Stop();
        }
        break;
      case ProgrammingState::STOPPING:
        if (attached && state != IDLE) {
          if (state == ABORTED || state == FINISHED) {
            qInfo() << "Erasing the tag's log";
            if (!tag.Erase()) {
              fail(tr("The tag refused the erase."));
              break;
            }
            wait_ticks_ = 0;
            programming_state = ProgrammingState::ERASING;
            break;
          }
          // Still stopping, testing, or finishing an earlier erase.
          if (++wait_ticks_ > 30)
            fail(tr("The tag stayed %1. Stop and erase it in Tag Monitor, then "
                    "program again.").arg(TagState_Name(state).c_str()));
          break;
        }
        programming_state = ProgrammingState::ERASING;
        [[fallthrough]];
      case ProgrammingState::ERASING:
        if (attached && (state == sRESET || state == ABORTED || state == FINISHED))
          break;  // the erase sweep is still running
        if (attached && state != IDLE) {
          fail(tr("The tag did not return to IDLE after its erase (it is %1).")
                   .arg(TagState_Name(state).c_str()));
          break;
        }
        if (attached)
          Detach();
        // Here we check if this is an erase only run (no programming or testing)
        if (erase_only) {
          programming_state = ProgrammingState::FINISHED;
          qDebug() << "Finished";
          break;
        }
        programming_state = ProgrammingState::PROGRAMMING;
        qDebug() << "Programming";
        [[fallthrough]];
      case ProgrammingState::PROGRAMMING:
        if (flashTag()) {
          fail(tr("STM32_Programmer_CLI failed; its output is in the log below."));
          break;
        }
        wait_ticks_ = 0;
        programming_state = ProgrammingState::CONNECTING;
        break;
      case ProgrammingState::CONNECTING:
        // The new image needs a moment to start before it answers.
        if (!tag.IsAttached() && !(have_base_ && Attach())) {
          if (++wait_ticks_ > 5)
            fail(plan_.erase == FlashPlan::Erase::All
                     ? tr("Programmed, but the tag does not answer. Power-cycle it "
                          "and press Connect.")
                     : tr("Programmed, but the tag does not answer through the base."));
          break;
        }
        qDebug() << "Testing";
        tag.SetRtc();
        tag.Test(RUN_ALL);
        programming_state = ProgrammingState::TESTING;
        break;
      case ProgrammingState::TESTING:
        if (attached && state == TEST)
          break;
        qDebug() << "Tests Complete";
        tag.SetRtc();
        // Start and stop once, then erase through the state machine again
        // (erase_only), so the new firmware lays out its own log and
        // persistent region before the tag is deployed.
        if (tag.GetConfig(config)) {
          tag.Start(config);
          programming_state = ProgrammingState::READY;
          erase_only = true;
        } else {
          fail(tr("The programmed tag did not report its configuration."));
        }
        break;
      case ProgrammingState::FINISHED:
        qDebug() << "Finished";
        Detach();
        break;
      case ProgrammingState::FAILED:
        break;
  }

  emit ProgrammingStateUpdate(programming_state);
  if ((programming_state != ProgrammingState::FINISHED) && (programming_state != ProgrammingState::FAILED))
     QTimer::singleShot(1000, [this, erase_only](){this->MainWindow::programStateMachine(erase_only);});
}

void MainWindow::on_programButton_clicked(){
  programming_state = ProgrammingState::READY;
  if (!have_base_)
    findBase();
  if (have_base_ && !tag.IsAttached())
    Attach();

  if (program.isEmpty() || !QFile::exists(program)){
    QMessageBox::warning(this, tr("Program Tag"),
                         tr("STM32_Programmer_CLI was not found. Choose it with the "
                            "programming tool button."));
    return;
  }

  plan_ = planFlash(ui.fileName->text());
  if (plan_.erase == FlashPlan::Erase::Invalid) {
    QMessageBox::warning(this, tr("Program Tag"), plan_.error);
    return;
  }
  if (tag.IsAttached() && (tag_type == IMUTAG) != (plan_.pageSize == 4096)) {
    QMessageBox::warning(
        this, tr("Program Tag"),
        tr("This image is for an %1 tag, and the attached tag is %2.")
            .arg(plan_.pageSize == 4096 ? "STM32U375 (IMUTag)" : "STM32L432",
                 QString::fromStdString(std::string(TagType_Name(tag_type)))));
    return;
  }
  if (!confirmProgram(plan_))
    return;
  QSettings().setValue("lastImage", ui.fileName->text());

  CustomMessageBox msgBox = CustomMessageBox("Programming Tag",this);
  msgBox.setWindowTitle(tr("Programming Tag"));

  if (tag.IsAttached()) {
    Status status;
    int erase_sector_maximum = roundedSectorCount(external_flash_size, sector_size);
    if (tag.GetStatus(status))
      erase_sector_maximum = eraseSectorMaximum(status);
    msgBox.setMaximum(erase_sector_maximum);
    connect(this,&MainWindow::SectorsErased,&msgBox,&CustomMessageBox::setValue);
  }

  connect(&msgBox, &CustomMessageBox::OkButtonClicked, 
          [this](){this->MainWindow::programStateMachine(false);});

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
        case ProgrammingState::CONNECTING:
          msgBox.setText("Connecting");
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

  // Confirmed already: start at once rather than asking again.
  QTimer::singleShot(0, &msgBox, &CustomMessageBox::start);
  msgBox.exec();
  if (programming_state == ProgrammingState::FAILED) {
    QMessageBox::warning(this, tr("Program Tag"),
                         tr("Programming failed.\n\n%1").arg(failure_));
  } else if (ui.info_testStatus->text() != "ALL_PASSED") {
    QMessageBox::warning(this, tr("Program Tag"),
                         tr("Programmed, but the self-test reported %1.")
                             .arg(ui.info_testStatus->text()));
  } else {
    QMessageBox::information(this, tr("Program Tag"),
                             tr("Programmed and tested: ALL_PASSED. The tag is "
                                "erased and IDLE."));
  }
}

void MainWindow::on_fileSelectButton_clicked()
{
  const QString last = ui.fileName->text();
  QString filename = HostFileDialog::getOpenFileName(
      this, tr("Firmware image to program"),
      last.isEmpty() ? QDir::homePath() : QFileInfo(last).absolutePath(),
      tr("Firmware ELF (*.elf);;All Files (*)"));
  if (!filename.isNull())
    setImage(filename);
}

void MainWindow::on_toolSelectButton_clicked()
{
  QString filename = HostFileDialog::getOpenFileName(
      this, tr("STM32 Programming CLI Tool"),
      program.isEmpty() ? QDir::homePath() : QFileInfo(program).absolutePath());
  if (!filename.isNull()) {
    ui.programmingTool->setText(filename);
    program = filename;
    QSettings().setValue("programmer", filename);
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

void MainWindow::on_connectButton_clicked(){
  if (!have_base_ && !findBase())
    return;
  if (!Attach())
    ui.statusBar->showMessage(tr("No tag answers through the base."));
}
void MainWindow::on_disconnectButton_clicked(){
  Detach();
}
