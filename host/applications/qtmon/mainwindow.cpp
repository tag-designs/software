/**
 * @file    mainwindow.cpp
 * @brief   qtmonitor's main window: attach, status polling, tag commands and
 *          downloads.
 *
 * @details The maintainer-only fixture replay and screenshot capture are in
 *          mainwindow_fixture.cpp.
 */


#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QTabWidget>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTime>
#include <QThread>
#include <QTimer>
#include <QLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QPromise>

#include <ctime>
#include <fstream>
#include <iomanip>
#include <utility>

#include <streambuf>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qtfiledialog.h"

#include "tag.pb.h"

#include "tagconfiguration/configtab.h"
#include "tagstatusstrip.h"
#include "abstractdownload.h"
#include "taglogwriter.h"


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

bool isDownloadableState(TagState state)
{
  // EXCEPTION is no longer collecting data, so allow users to reach the
  // download controls instead of requiring a reset that could erase evidence.
  return state == FINISHED || state == ABORTED || state == EXCEPTION;
}

/**
 * @brief Reports whether qtmonitor exposes the Calibrate action for a tag.
 *
 * @details Only tags with firmware support for the calibration state should
 *          present the button. Hiding unsupported actions keeps tags without
 *          calibration workflows from showing an attractive but ineffective
 *          control.
 *
 * @param[in] tag_type   Tag type from TagInfo or fixture metadata.
 *
 * @return true for tag families with calibration support; false otherwise.
 */
bool tagSupportsCalibration(TagType tag_type)
{
  return tag_type == COMPASSTAG || tag_type == IMUTAG;
}

} // namespace

/**
 * @brief Creates the main window and chooses live or fixture replay startup.
 *
 * @details With default options the constructor follows normal qtmonitor
 *          behavior by probing USB and attaching to a live tag. Fixture options
 *          load captured protobuf JSON, populate the same widgets, and avoid
 *          starting the live status timer. Parameters are documented in
 *          mainwindow.h.
 */
MainWindow::MainWindow(const MainWindowOptions &options, QWidget *parent)
    : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Commands are actions in the menus and popup menus; Start and Stop are
  // also buttons on the status strip below the tab bar.
  createActions();
  status_strip_ = new TagStatusStrip(start_action_, stop_action_);
  placeStatusStripBelowTabs();
  createMenus();
  ui.mainToolBar->hide();  // no toolbar actions yet
  // Spare height on the Tag page goes below its groups, not between rows.
  qobject_cast<QBoxLayout *>(ui.widget_3->layout())->addStretch(1);
  screenshotDir = resolveScreenshotDir(options.screenshotDir);
  fakeFixturePath = options.fakeFixturePath;
  captureStartupScreenshotOnStartup = options.captureStartupScreenshot;
  captureMainScreenshotsOnStartup = options.captureMainScreenshots;
  captureConfigScreenshotsOnStartup = options.captureConfigScreenshots;
  captureErrorLogScreenshotOnStartup = options.captureErrorLogScreenshot;

  // start on main tab

  ui.mainTabWidget->setCurrentIndex(0);

  // Change Main Window title

  QString title = QString::fromStdString("Tag Monitor v") + QString::number(version);

  setWindowTitle(title);

  // Tag state poll timer

  connect(this, SIGNAL(StateUpdate(TagState)), ui.configtab, SLOT(StateUpdate(TagState)));
  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));

  // Normal mode attaches to a physical tag and starts the polling timer. Fake
  // fixture mode is intentionally routed through the same UI population helpers
  // so documentation screenshots exercise the real qtmonitor widgets.
  if (!fakeFixturePath.isEmpty()) {
    if (loadFakeFixture(fakeFixturePath)) {
      setupFakeFixture();
      applyFakeState(options.fakeState);
    } else {
      setAttachedUiEnabled(false);
    }
  } else if (options.skipAutoAttach) {
    setAttachedUiEnabled(false);
    ui.configtab->Detach();
  } else {
    attachTag();
  }
  updateActions();
}

MainWindow::~MainWindow()
{
  ui.configtab->Detach();
  if (!fakeMode) {
    tag.Detach();
  }
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

void MainWindow::logTagDebugMessage(const QString &message)
{
  for (const QChar ch : message) {
    if (ch == QLatin1Char('\r'))
      continue;
    if (ch == QLatin1Char('\n')) {
      qDebug().noquote() << "Log:" << tag_debug_line_buffer;
      tag_debug_line_buffer.clear();
      continue;
    }
    tag_debug_line_buffer.append(ch);
  }
}

/**
 * @brief Enables or disables top-level widgets that depend on an attachment.
 *
 * @details Both live mode and fixture mode use this helper so fake replay
 *          screenshots have the same broad UI affordances as a connected tag.
 *          State-specific actions are refined by applyStatus(), through
 *          updateActions().
 *
 * @param[in] enabled   true when a live or fake tag is considered attached.
 */
void MainWindow::setAttachedUiEnabled(bool enabled)
{
  attached_ui_ = enabled;
  ui.TagInformation->setEnabled(enabled);
  ui.datadownloadgroupBox->setEnabled(false);
  if (!enabled) {
    status_strip_->setDetached();
  }
  updateActions();
}

/**
 * @brief Copies tag identity metadata into the Tag Information widgets.
 *
 * @param[in] info   TagInfo message read from hardware or fixture JSON.
 *
 * @post The cached tag type and external flash size reflect @p info.
 */
void MainWindow::populateTagInfo(const TagInfo &info)
{
  ui.info_tagtype->setText(QString::fromStdString(TagType_Name(info.tag_type())));
  ui.info_boardname->setText(QString::fromStdString(info.board_desc()));
  ui.info_firmware->setText(QString::fromStdString(info.firmware()));
  ui.info_gitHash->setText(QString::fromStdString(info.githash()));
  ui.info_gitUrl->setText(QString::fromStdString(info.gitrepo()));
  ui.info_uuid->setText(QString::fromStdString(info.uuid()));
  ui.info_flash->setText(QString::number(info.intflashsz()) + "KB");
  ui.info_flash_ext->setText(QString::number(info.extflashsz() / (1024 * 1024))
                             + "MB");
  external_flash_size = info.extflashsz();
  ui.info_buildDate->setText(QString::fromStdString(info.build_time()));
  ui.info_srcpath->setText(QString::fromStdString(info.source_path()));
  tag_type = info.tag_type();
  tag_uuid_ = QString::fromStdString(info.uuid());
  status_strip_->setAttached(tag_type, tag_uuid_);
}

/**
 * @brief Populates the Configuration tab from live or fixture configuration.
 *
 * @details Live mode attaches child configuration widgets to the active Tag
 *          object before setting fields. Fixture mode skips Tag attachment and
 *          marks the tab as display-only so Start and Read are guarded no-ops.
 *
 * @param[in] config       Configuration protobuf to display.
 * @param[in] fixtureMode  true for documentation replay, false for live tag IO.
 */
void MainWindow::attachConfig(const Config &config, bool fixtureMode)
{
  ui.configtab->SetFixtureMode(fixtureMode);
  if (!fixtureMode) {
    ui.configtab->Attach(tag);
  }
  ui.configtab->SetConfig(config);
}

/**
 * @brief Applies one status sample to all Tag State controls.
 *
 * @details This is the shared state-rendering path for timer-driven live
 *          updates and fake replay screenshots. It updates labels, log counts,
 *          test status, state-specific buttons, download availability, and
 *          Configuration-tab enablement.
 *
 * @param[in] status   Status protobuf read from hardware or fixture replay.
 * @param[in] voltage  Last reported tag voltage in volts.
 *
 * @post current_state is updated and StateUpdate is emitted.
 */
void MainWindow::applyStatus(const Status &status, float voltage)
{
  setAttachedUiEnabled(true);
  ui.internalCount->setText(QString::number(status.internal_data_count()));
  if (status.external_data_count() != 0) {
    ui.externalCount->setText(QString::number(status.external_data_count()));
    ui.ExternalLog->setVisible(true);
  } else {
    ui.ExternalLog->setVisible(false);
  }

  if (!status.debug_message().empty()) {
    logTagDebugMessage(QString::fromStdString(status.debug_message()));
  }

  double timeerr = QDateTime::currentMSecsSinceEpoch();
  timeerr = status.millis() - timeerr;
  status_strip_->setStatus(status.state(), timeerr / 1000.0, voltage,
                           status.test_status());
  if (test_pending_) {
    // The tag runs every test in one pass, usually faster than this poll, so
    // TEST is rarely seen. Wait for a finished result from a poll at least
    // kTestMinimumMs after the command, so the previous run's result, which a
    // poll can see before the tag has begun, is not taken for this one.
    const qint64 elapsed = test_started_.msecsTo(QDateTime::currentDateTime());
    const QString who = QString("%1 %2")
                            .arg(QString::fromStdString(std::string(TagType_Name(tag_type))),
                                 tag_uuid_);
    if (elapsed >= kTestMinimumMs && status.test_status() != TEST_RUNNING) {
      test_pending_ = false;
      qInfo().noquote() << "Self-test on" << who << "finished:"
                        << QString::fromStdString(std::string(TestResult_Name(status.test_status())));
    } else if (elapsed > kTestTimeoutMs) {
      test_pending_ = false;
      qWarning().noquote() << "Self-test on" << who << "gave no result within"
                           << kTestTimeoutMs / 1000 << "s";
    } else {
      status_strip_->setTestRunning();
    }
  }
  ui.datadownloadgroupBox->setEnabled(isDownloadableState(status.state()) &&
                                      !downloading_);

  current_state = status.state();
  emit StateUpdate(current_state);
  updateActions();
  if (fakeMode) {
    ui.configtab->SetFixtureMode(true);
  }

  if (status.state() == IDLE) {
    emit IdleState();
  }

  if (status.state() == sRESET) {
    emit EraseProgress(status.sectors_erased(), eraseSectorMaximum(status));
    emit SectorsErased(status.sectors_erased());
  }
}

bool MainWindow::Attach()
{
  if (tag.IsAttached())
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Already Attached to Tag");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
  }

  std::vector<UsbDev> usbdevs;
  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("No Tag Bases Found");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    return false;
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
    //inputDialog->setOption(QInputDialog::NoButtons);
    inputDialog->setOption(QInputDialog::UseListViewForComboBoxItems);
    inputDialog->setComboBoxItems(items);
    inputDialog->setWindowTitle("Available Bases");
    inputDialog->setLabelText("Please select a base");
    if (!inputDialog->exec())
      return false;
    QString item = inputDialog->textValue();
    if (!item.isEmpty())
    {
      index = items.indexOf(item);
    }
    if (index > usbdevs.size())
      index = 0;
  }

  if (tag.Attach(usbdevs[index]))
  {
    std::string str;
    int size;
    Config config;
    Status status;
    TagInfo info;
    tag_debug_line_buffer.clear();
    tag.GetTagInfo(info);
    tag.GetConfig(config);
    tag.GetStatus(status);
    if (!status.debug_message().empty()){
      logTagDebugMessage(QString::fromStdString(status.debug_message()));
    }

    // check qtmonitor version 

    float min_version = 2.0;//info.qtmonitor_min_version();

    if (min_version > version) {
      QMessageBox msgBox;
      QString message = QString("monitor version %1 less than required version %2").arg(version).arg(min_version);
      msgBox.setWindowTitle("Warning");
      msgBox.setText(message);
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.exec();
      detachTag();
      return false;
    }

    populateTagInfo(info);
    attachConfig(config, false);
    ui.errorTab->Attach(tag);

     

    // start the StateUpdate timer
    
    TriggerUpdate();
    timer.start(400);
    return true;
  }
  else
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Could not attach to tag");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();

    //timer.stop();
    //TriggerUpdate(); // should this be here?
    return false;
  }
  
}

// While tag is attached, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{

  Status status;

  if (tag.IsAttached())
  {
    std::string msg;

    float voltage = 0.0f;
    tag.Voltage(voltage);

    // update status data

    if (tag.GetStatus(status))
    {
      applyStatus(status, voltage);

      if (status.state() == CALIBRATE)
      {
        Ack ack;
        float mx,my,mz,ax,ay,az;
        tag.GetCalibrationLog(ack);
        if (ack.has_calibration_log()) {
            for(auto const &sdata : ack.calibration_log().data())
            {
              bool saw_sensor_data = false;
              if (sdata.has_mag()){
                  mx = sdata.mag().mx();
                  my = sdata.mag().my();
                  mz = sdata.mag().mz();
                  qInfo() << "Mag: " << mx << my << mz;
                  saw_sensor_data = true;
              }

              if (sdata.has_accel()){
                ax = sdata.accel().ax();
                ay = sdata.accel().ay();
                az = sdata.accel().az();
                qInfo() << "Accel: " << ax << ay << az;
                saw_sensor_data = true;
              }

              if (!saw_sensor_data) {
                qInfo() << "Calibration sample had no sensor data";
              }
            }
        }
        else {
          qInfo() << "No calibration log";
        }
      }

      /*
      if ((status.state() == CALIBRATE)  && status.has_sensors()){
        QString sensors = QString::fromStdString(status.sensors().DebugString());
        sensors = sensors.replace("\n",", ");
        qInfo() << "Sensors: " << sensors;
      }*/
    }
  }
}




/********************************************
 *        Actions and menus
 ********************************************/

void MainWindow::createActions()
{
  auto make = [this](const QString &text, const QString &tip, auto slot) {
    auto *action = new QAction(text, this);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    connect(action, &QAction::triggered, this, slot);
    return action;
  };

  attach_action_ = make(tr("Attach"), tr("Find a tag base and attach to its tag"),
                        &MainWindow::attachTag);
  detach_action_ = make(tr("Detach"), tr("Detach from the tag"), &MainWindow::detachTag);
  stop_action_ = make(tr("Stop"), tr("Stop the tag"), &MainWindow::stopTag);
  sync_action_ = make(tr("Synchronize Clock"),
                      tr("Set the tag's clock from this computer's"),
                      &MainWindow::syncClock);
  test_action_ = make(tr("Self-Test"), tr("Run the tag's self-tests"),
                      &MainWindow::testTag);
  calibrate_action_ = make(tr("Calibrate"), tr("Put the tag in its calibration state"),
                           &MainWindow::calibrateTag);
  erase_action_ = make(tr("Erase..."),
                       tr("Erase the tag's data and return it to IDLE"),
                       &MainWindow::eraseTag);
  download_action_ = make(tr("Download Data..."),
                          tr("Download the tag's data log to a file"),
                          &MainWindow::downloadData);

  // The configuration commands belong to the Configuration tab.
  ConfigTab *config = ui.configtab;
  start_action_ = new QAction(tr("Start"), this);
  start_action_->setToolTip(tr("Write the configuration to the tag and start it"));
  connect(start_action_, &QAction::triggered, config, &ConfigTab::StartTag);
  read_config_action_ = new QAction(tr("Read Configuration"), this);
  read_config_action_->setToolTip(tr("Read the tag's configuration"));
  connect(read_config_action_, &QAction::triggered, config, &ConfigTab::ReadFromTag);
  save_config_action_ = new QAction(tr("Save Configuration..."), this);
  save_config_action_->setToolTip(tr("Save the configuration to a file"));
  connect(save_config_action_, &QAction::triggered, config, &ConfigTab::SaveToFile);
  restore_config_action_ = new QAction(tr("Restore Configuration..."), this);
  restore_config_action_->setToolTip(
      tr("Restore a configuration file over the tag's configuration"));
  connect(restore_config_action_, &QAction::triggered, config,
          &ConfigTab::RestoreFromFile);

  quit_action_ = new QAction(tr("Quit"), this);
  quit_action_->setShortcut(QKeySequence::Quit);
  connect(quit_action_, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::createMenus()
{
  // In the window rather than the macOS menu bar, as in sensorviz.
  menuBar()->setNativeMenuBar(false);

  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addAction(save_config_action_);
  file->addAction(restore_config_action_);
  file->addSeparator();
  file->addAction(download_action_);
  file->addAction(ui.errorTab->saveAction());
  file->addSeparator();
  file->addAction(quit_action_);

  QMenu *tagMenu = menuBar()->addMenu(tr("&Tag"));
  tagMenu->addAction(attach_action_);
  tagMenu->addAction(detach_action_);
  tagMenu->addSeparator();
  tagMenu->addAction(read_config_action_);
  tagMenu->addAction(start_action_);
  tagMenu->addAction(stop_action_);
  tagMenu->addSeparator();
  tagMenu->addAction(sync_action_);
  tagMenu->addAction(test_action_);
  tagMenu->addAction(calibrate_action_);
  tagMenu->addSeparator();
  tagMenu->addAction(erase_action_);

  QMenu *log = menuBar()->addMenu(tr("&Log"));
  log->addMenu(ui.errorTab->logLevelMenu());
  log->addAction(ui.errorTab->clearAction());

  // Popup menus. Children without a menu of their own (group boxes, labels)
  // pass a right-click up to these widgets.
  const QList<QAction *> tagCommands = {
      attach_action_, detach_action_, sync_action_, test_action_,
      calibrate_action_, erase_action_, download_action_};
  ui.targetTab->setContextMenuPolicy(Qt::ActionsContextMenu);
  ui.targetTab->addActions(tagCommands);
  status_strip_->setContextMenuPolicy(Qt::ActionsContextMenu);
  status_strip_->addActions(tagCommands);
  ui.configtab->setContextMenuPolicy(Qt::ActionsContextMenu);
  ui.configtab->addActions({read_config_action_, restore_config_action_,
                            save_config_action_, start_action_});
}

void MainWindow::placeStatusStripBelowTabs()
{
  // QTabWidget draws its tab bar directly on its pages, leaving no room
  // between them. So its own bar is hidden and a standalone QTabBar above the
  // strip stands in for it, kept in step in both directions; code that
  // selects a tab on mainTabWidget still works.
  QTabWidget *tabs = ui.mainTabWidget;
  tab_bar_ = new QTabBar();
  tab_bar_->setExpanding(false);
  for (int i = 0; i < tabs->count(); ++i) {
    tab_bar_->addTab(tabs->tabText(i));
    tab_bar_->setTabToolTip(i, tabs->tabToolTip(i));
  }
  tab_bar_->setCurrentIndex(tabs->currentIndex());
  tabs->tabBar()->hide();
  connect(tab_bar_, &QTabBar::currentChanged, tabs, &QTabWidget::setCurrentIndex);
  connect(tabs, &QTabWidget::currentChanged, tab_bar_, &QTabBar::setCurrentIndex);

  auto *tabRow = new QHBoxLayout();
  tabRow->addStretch(1);
  tabRow->addWidget(tab_bar_);
  tabRow->addStretch(1);

  auto *central = qobject_cast<QBoxLayout *>(ui.centralWidget->layout());
  central->insertLayout(0, tabRow);
  central->insertWidget(1, status_strip_);
}

void MainWindow::updateActions()
{
  const bool attached = attached_ui_;
  const TagState state = current_state;
  const bool idle = attached && state == IDLE;

  attach_action_->setEnabled(!attached);
  detach_action_->setEnabled(attached);
  sync_action_->setEnabled(idle);
  test_action_->setEnabled(idle && !test_pending_);
  calibrate_action_->setVisible(attached && tagSupportsCalibration(tag_type));
  calibrate_action_->setEnabled(idle && tagSupportsCalibration(tag_type));
  erase_action_->setEnabled(attached && (state == FINISHED || state == ABORTED));
  download_action_->setEnabled(attached && isDownloadableState(state) && !downloading_);
  stop_action_->setEnabled(attached && (state == CONFIGURED || state == RUNNING ||
                                        state == HIBERNATING || state == CALIBRATE));

  // The configuration commands need a configuration in the tab; all but Save
  // also need an IDLE tag, as editing does (ConfigTab::StateUpdate()).
  const bool config = ui.configtab->isActive();
  save_config_action_->setEnabled(config);
  restore_config_action_->setEnabled(config && idle);
  read_config_action_->setEnabled(config && idle);
  start_action_->setEnabled(config && idle);
}

/********************************************
 *        Tag commands
 ********************************************/

void MainWindow::attachTag()
{
  if (fakeMode) {
    return;
  }
  // A successful Attach() has already shown the first status, which marked
  // the tag attached (applyStatus()).
  if (!Attach()) {
    setAttachedUiEnabled(false);
  }
}

void MainWindow::detachTag()
{
  if (fakeMode) {
    return;
  }
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  test_pending_ = false;
  setAttachedUiEnabled(false);
  ui.configtab->Detach();
  tag_debug_line_buffer.clear();
  updateActions();
}

void MainWindow::syncClock()
{
  if (fakeMode) {
    return;
  }
  if (!tag.SetRtc()) {
    QMessageBox syncFailedBox;
    syncFailedBox.setWindowTitle(tr("Sync Failed"));
    syncFailedBox.setIcon(QMessageBox::Warning);
    syncFailedBox.setText(tr("The tag did not accept the clock sync command."));
    const std::string message = tag.DebugMessage();
    if (!message.empty())
    {
      syncFailedBox.setInformativeText(QString::fromStdString(message));
      syncFailedBox.setDetailedText(QString::fromStdString(message));
    }
    syncFailedBox.exec();
    return;
  }
  TriggerUpdate();
}

void MainWindow::stopTag()
{
  if (fakeMode) {
    return;
  }
  if (!tag.Stop()) {
    QMessageBox::warning(this,tr("Stop Failed"),
                         tr("The tag did not accept the stop command."));
    return;
  }
  QTimer::singleShot(250,this,[this]() { TriggerUpdate(); });
}

void MainWindow::calibrateTag()
{
  if (fakeMode) {
    return;
  }
  if (!tagSupportsCalibration(tag_type)) {
    return;
  }
  tag.Calibrate();
}

void MainWindow::testTag()
{
  if (fakeMode) {
    return;
  }
  if (current_state == IDLE)
  {
    std::string msg;
    tag.Test(RUN_ALL); // need to check return !
    test_pending_ = true;
    test_started_ = QDateTime::currentDateTime();
    qInfo().noquote() << "Self-test started on"
                      << QString::fromStdString(std::string(TagType_Name(tag_type)))
                      << tag_uuid_;
    status_strip_->setTestRunning();
    updateActions();
  }
}

void MainWindow::eraseTag()
{
  if (fakeMode) {
    return;
  }
  QMessageBox msgBox;
  msgBox.setWindowTitle("Reset Tag");
  msgBox.setText("Erase tag state and data ?");
  msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  msgBox.setDefaultButton(QMessageBox::Cancel);
  int ret = msgBox.exec();
  if (ret == QMessageBox::Ok)
  {
    Status status;
    int erase_sector_maximum = roundedSectorCount(external_flash_size, sector_size);
    if (tag.GetStatus(status))
      erase_sector_maximum = eraseSectorMaximum(status);

    if (!tag.Erase())
    {
      QMessageBox::warning(this,tr("Erase Failed"),
                           tr("The tag did not accept the erase command."));
    } else {
      if (!timer.isActive())
        timer.start(400);

      QProgressDialog progress("Erasing flash...", "Close", 0,
                               erase_sector_maximum, this);
      progress.setWindowModality(Qt::WindowModal);
      progress.setMinimumDuration(0);
      connect(this,&MainWindow::EraseProgress,&progress,
              [&progress](int value, int maximum) {
                if (maximum > progress.maximum())
                  progress.setRange(0, maximum);
                progress.setValue(value);
              });
      connect(this,SIGNAL(SectorsErased(int)),&progress,SLOT(setValue(int)));
      connect(this,SIGNAL(IdleState()), &progress, SLOT(close()));
      progress.exec();
    }
  }
}

// Start a tag-data download from the UI. The window chooses a default storage
// format for the current tag type and owns only the user interaction. The
// download loop and file/database writing are delegated to AbstractDownload and
// the host-lib TagLogWriter implementations.

void MainWindow::downloadData()
{
  if (fakeMode) {
    return;
  }
  const TagLogStorageFormat storage_format = defaultTagLogStorageFormat(tag_type);
  const std::vector<TagLogStorageFormat> formats = supportedTagLogStorageFormats(tag_type);
  // SQLite is the only download format, so a tag type without a SQLite schema
  // cannot be saved.
  if (formats.empty()) {
    QMessageBox::warning(this, tr("Cannot save log"),
                         tr("This tag type has no SQLite log format, so its data "
                            "cannot be downloaded."));
    return;
  }
  QStringList filter_list;
  for (TagLogStorageFormat format : formats) {
    filter_list << QString::fromStdString(tagLogFileFilter(format));
  }
  QString selected_filter;
  QString filter = filter_list.join(";;");
  QString initial_path = QDir::homePath()
      + "/untitled"
      + QString::fromStdString(defaultTagLogExtension(storage_format));
  
  QString fileName = HostFileDialog::getSaveFileName(
      this, tr("Save File"), initial_path, filter, &selected_filter);

  if (fileName.isEmpty()) {
      qDebug() << "null filename";
      return;
  }

  TagLogStorageFormat selected_storage_format = storage_format;
  for (TagLogStorageFormat format : formats) {
    if (selected_filter == QString::fromStdString(tagLogFileFilter(format))) {
      selected_storage_format = format;
      break;
    }
  }

  const QString selected_extension =
      QString::fromStdString(defaultTagLogExtension(selected_storage_format));
  const QString selected_suffix = selected_extension.mid(1);
  const QFileInfo file_info(fileName);
  bool suffix_matches_supported_format = false;
  for (TagLogStorageFormat format : formats) {
    const QString suffix = QString::fromStdString(defaultTagLogExtension(format)).mid(1);
    if (file_info.suffix().compare(suffix, Qt::CaseInsensitive) == 0) {
      suffix_matches_supported_format = true;
      break;
    }
  }
  if (file_info.suffix().isEmpty()) {
    fileName += selected_extension;
  } else if (suffix_matches_supported_format
             && file_info.suffix().compare(selected_suffix, Qt::CaseInsensitive) != 0) {
    fileName = file_info.path() + "/" + file_info.completeBaseName() + selected_extension;
  }

  qDebug() <<  "connecting progess dialog";

  // Create Progress Dialog

  QProgressDialog *pd = new QProgressDialog("Downloading ..","Cancel",0,0,this);
  pd->setWindowModality(Qt::WindowModal);
  pd->setMinimumDuration(0);

  // This is deliberately an explicit format value instead of a hidden
  // tag-type switch inside AbstractDownload. A future UI can replace the
  // default with a user-selected format for tags that support more than one.
  AbstractDownload *dl = new AbstractDownload(tag, selected_storage_format, fileName.toStdString());
  QThread *download_thread = new QThread(this);
  dl->moveToThread(download_thread);

  connect(download_thread,&QThread::started,dl,&AbstractDownload::exec);
  connect(dl,&AbstractDownload::progressRangeChanged,pd,&QProgressDialog::setRange);
  connect(dl,&AbstractDownload::progressValueChanged,pd,&QProgressDialog::setValue);
  connect(dl,&AbstractDownload::downloadError,this,[this](const QString &message) {
    QMessageBox::critical(this,tr("Download Failed"),message);
  });
  connect(pd,&QProgressDialog::canceled,dl,&AbstractDownload::cancel,Qt::DirectConnection);
  connect(dl,&AbstractDownload::downloadFinished,pd,&QProgressDialog::close);
  connect(dl,&AbstractDownload::downloadFinished,download_thread,&QThread::quit);
  connect(dl,&AbstractDownload::downloadFinished,dl,&QObject::deleteLater);
  connect(download_thread,&QThread::finished,download_thread,&QObject::deleteLater);
  connect(download_thread,&QThread::finished,pd,&QObject::deleteLater);

  const bool restart_status_timer = timer.isActive();
  timer.stop();
  downloading_ = true;
  ui.datadownloadgroupBox->setEnabled(false);
  updateActions();
  connect(download_thread,&QThread::finished,this,[this,restart_status_timer]() {
    downloading_ = false;
    ui.datadownloadgroupBox->setEnabled(tag.IsAttached() &&
                                        isDownloadableState(current_state));
    updateActions();
    if (restart_status_timer && tag.IsAttached()) {
      timer.start(400);
      TriggerUpdate();
    }
  });

  qDebug() <<  "starting download";

  download_thread->start();
  pd->show();
  return;
}
