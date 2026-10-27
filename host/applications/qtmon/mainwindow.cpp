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
#include <QPushButton>
#include <QCloseEvent>
#include <QFileInfo>
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
#include "configshape.h"
#include "tagstatusstrip.h"
#include "qtuserguide.h"
#include "abstractdownload.h"
#include "taglogwriter.h"
#include "commands.h"
#include "startchecks.h"
#include "tagrules.h"


using qtmon::Command;


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
  connect(status_strip_, &TagStatusStrip::approvalClicked, ui.configtab,
          &ConfigTab::setApproved);
  placeStatusStripBelowTabs();
  createMenus();
  ui.mainToolBar->hide();  // no toolbar actions yet
  // Its only use was hover text for menu items, which the tooltips carry; an
  // empty bar reads as a strip that is not part of the window.
  ui.statusBar->hide();
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

  updateTitle();

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
  return qtmon::eraseSectorMaximum(status, tag_type, external_flash_size, sector_size);
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
  ui.configtab->SetAttachedTagType(tag_type);
  status_strip_->setAttached(tag_type, tag_uuid_);
}

/**
 * @brief Populates the Configuration tab from live or fixture configuration.
 *
 * @details Live mode attaches the tab to the Tag and keeps a document the
 *          user opened or created; see mainwindow.h.
 *
 * @param[in] config       Configuration protobuf to display.
 * @param[in] fixtureMode  true for documentation replay, false for live tag IO.
 */
void MainWindow::attachConfig(const Config &config, bool fixtureMode)
{
  ui.configtab->SetFixtureMode(fixtureMode);
  if (fixtureMode) {
    ui.configtab->SetConfig(config);
    return;
  }
  ui.configtab->Attach(tag);
  // A configuration the user opened or created stays: they may be about to
  // start this tag with it. Otherwise the tag's own is read in, asking first
  // if a previous tag's configuration has unsaved changes.
  switch (ui.configtab->source()) {
  case ConfigTab::Source::Defaults:
  case ConfigTab::Source::File:
    return;
  case ConfigTab::Source::Tag:
    if (!ui.configtab->confirmDiscard())
      return;
    break;
  case ConfigTab::Source::None:
    break;
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

  clock_offset_s_ =
      qtmon::clockOffsetSeconds(status.millis(), QDateTime::currentMSecsSinceEpoch());
  last_test_ = status.test_status();
  status_strip_->setStatus(status.state(), clock_offset_s_, voltage,
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
  ui.datadownloadgroupBox->setEnabled(qtmon::isDownloadableState(status.state()) &&
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
  connect(start_action_, &QAction::triggered, this, &MainWindow::startWithChecks);
  read_config_action_ = new QAction(tr("Read Configuration"), this);
  read_config_action_->setToolTip(tr("Read the tag's configuration"));
  connect(read_config_action_, &QAction::triggered, config, &ConfigTab::ReadFromTag);
  open_config_action_ = new QAction(tr("Open Configuration..."), this);
  open_config_action_->setToolTip(tr("Open a configuration file for editing"));
  open_config_action_->setShortcut(QKeySequence::Open);
  connect(open_config_action_, &QAction::triggered, config, &ConfigTab::Open);
  save_config_action_ = new QAction(tr("Save Configuration"), this);
  save_config_action_->setToolTip(tr("Save the configuration to its file"));
  save_config_action_->setShortcut(QKeySequence::Save);
  connect(save_config_action_, &QAction::triggered, config, &ConfigTab::Save);
  save_as_config_action_ = new QAction(tr("Save Configuration As..."), this);
  save_as_config_action_->setToolTip(tr("Save the configuration to a new file"));
  save_as_config_action_->setShortcut(QKeySequence::SaveAs);
  connect(save_as_config_action_, &QAction::triggered, config, &ConfigTab::SaveAs);
  reset_config_action_ = new QAction(tr("Reset to Defaults"), this);
  reset_config_action_->setToolTip(
      tr("Replace the configuration's values with its tag type's defaults"));
  connect(reset_config_action_, &QAction::triggered, config, &ConfigTab::ResetToDefaults);

  // New ▸ one entry per tag type that has a default configuration.
  new_config_menu_ = new QMenu(tr("New Configuration"), this);
  for (TagType type : ConfigShape::tagTypes()) {
    QAction *action = new_config_menu_->addAction(
        QString::fromStdString(std::string(TagType_Name(type))));
    action->setToolTip(tr("Start from this tag type's default configuration"));
    connect(action, &QAction::triggered, config,
            [config, type]() { config->NewFromDefaults(type); });
  }
  recent_config_menu_ = new QMenu(tr("Open Recent"), this);
  connect(recent_config_menu_, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);

  connect(config, &ConfigTab::documentChanged, this, [this]() {
    updateTitle();
    updateActions();
  });
  // Approval belongs to the document, which withdraws it when the values
  // change; the strip's box shows it.
  connect(config, &ConfigTab::contentChanged, this, [this]() {
    if (status_strip_)
      status_strip_->setApproved(ui.configtab->isApproved());
  });

  guide_action_ = make(tr("Tag Monitor User Guide"),
                       tr("Open the Tag Monitor page of the user manual"),
                       &MainWindow::showUserGuide);
  about_action_ = make(tr("About Tag Monitor"), tr("Version and purpose"),
                       &MainWindow::showAbout);

  quit_action_ = new QAction(tr("Quit"), this);
  quit_action_->setShortcut(QKeySequence::Quit);
  connect(quit_action_, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::createMenus()
{
  // In the window rather than the macOS menu bar, as in sensorviz.
  menuBar()->setNativeMenuBar(false);

  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addMenu(new_config_menu_);
  file->addAction(open_config_action_);
  file->addMenu(recent_config_menu_);
  file->addAction(save_config_action_);
  file->addAction(save_as_config_action_);
  file->addAction(reset_config_action_);
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
  tagMenu->addSeparator();
  tagMenu->addAction(erase_action_);

  QMenu *log = menuBar()->addMenu(tr("&Log"));
  log->addMenu(ui.errorTab->logLevelMenu());
  log->addAction(ui.errorTab->clearAction());

  QMenu *help = menuBar()->addMenu(tr("&Help"));
  help->addAction(guide_action_);
  help->addAction(about_action_);

  // Popup menus. Children without a menu of their own (group boxes, labels)
  // pass a right-click up to these widgets.
  const QList<QAction *> tagCommands = {
      attach_action_, detach_action_, sync_action_, test_action_,
      erase_action_, download_action_};
  ui.targetTab->setContextMenuPolicy(Qt::ActionsContextMenu);
  ui.targetTab->addActions(tagCommands);
  status_strip_->setContextMenuPolicy(Qt::ActionsContextMenu);
  status_strip_->addActions(tagCommands);
  ui.configtab->setContextMenuPolicy(Qt::ActionsContextMenu);
  auto *separator = new QAction(this);
  separator->setSeparator(true);
  ui.configtab->addActions({read_config_action_, open_config_action_,
                            save_config_action_, save_as_config_action_,
                            reset_config_action_, separator, start_action_});
}

QStringList MainWindow::startCheckFailures() const
{
  QStringList failures;
  for (qtmon::StartCheck check :
       qtmon::startCheckFailures(clock_offset_s_, last_test_, ui.configtab->isApproved())) {
    switch (check) {
    case qtmon::StartCheck::Clock:
      failures << tr("Clock is %1 s off (limit %2 s).")
                      .arg(QString::number(clock_offset_s_, 'g', 3))
                      .arg(qtmon::kClockToleranceS);
      break;
    case qtmon::StartCheck::SelfTest:
      failures << tr("Self-test is %1.")
                      .arg(QString::fromStdString(std::string(TestResult_Name(last_test_))));
      break;
    case qtmon::StartCheck::Approval:
      failures << tr("Configuration is not approved.");
      break;
    }
  }
  return failures;
}

void MainWindow::startWithChecks()
{
  const QStringList failures = startCheckFailures();
  if (!failures.isEmpty()) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Start Tag"));
    box.setText(tr("Start the tag anyway?"));
    box.setInformativeText(QStringLiteral("\u2022 ") +
                           failures.join(QStringLiteral("\n\u2022 ")));
    QPushButton *anyway = box.addButton(tr("Start Anyway"), QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() != anyway)
      return;
    qWarning().noquote() << "Starting"
                         << QString::fromStdString(std::string(TagType_Name(tag_type)))
                         << tag_uuid_ << "although:" << failures.join(" ");
  }
  ui.configtab->StartTag();
}

void MainWindow::updateTitle()
{
  QString title = tr("Tag Monitor v%1").arg(version);
  const QString name = ui.configtab->documentName();
  if (!name.isEmpty())
    title += QStringLiteral(" \u2014 ") + name + QStringLiteral("[*]");
  setWindowTitle(title);
  setWindowModified(ui.configtab->isModified());
}

void MainWindow::fillRecentMenu()
{
  recent_config_menu_->clear();
  const QStringList files = ConfigTab::recentFiles();
  if (files.isEmpty()) {
    recent_config_menu_->addAction(tr("No recent files"))->setEnabled(false);
    return;
  }
  for (const QString &path : files) {
    QAction *action = recent_config_menu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, ui.configtab,
            [this, path]() { ui.configtab->OpenFile(path); });
  }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (ui.configtab->confirmDiscard())
    event->accept();
  else
    event->ignore();
}

void MainWindow::showUserGuide()
{
  HostUserGuide::open(this, tr("Tag Monitor User Guide"), QStringLiteral("qtmonitor"));
}

void MainWindow::showAbout()
{
  QMessageBox::about(this, tr("About Tag Monitor"),
                     tr("Tag Monitor v%1\n\nAttach to a tag through its USB base, check "
                        "it, configure and start it, and download its data.")
                         .arg(version));
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
  qtmon::CommandInputs in;
  in.attached = attached_ui_;
  in.state = current_state;
  in.tag_type = tag_type;
  in.document = ui.configtab->hasDocument();
  in.document_type = ui.configtab->documentTagType();
  in.downloading = downloading_;
  in.test_pending = test_pending_;
  for (Command command : qtmon::kAllCommands) {
    QAction *action = actionFor(command);
    const qtmon::Availability a = qtmon::availability(command, in);
    action->setVisible(a != qtmon::Availability::Hidden);
    action->setEnabled(a == qtmon::Availability::Enabled);
  }
  status_strip_->setApprovalAvailable(in.document);
}

QAction *MainWindow::actionFor(Command command) const
{
  switch (command) {
  case Command::Attach:
    return attach_action_;
  case Command::Detach:
    return detach_action_;
  case Command::Read:
    return read_config_action_;
  case Command::Start:
    return start_action_;
  case Command::Stop:
    return stop_action_;
  case Command::Sync:
    return sync_action_;
  case Command::Test:
    return test_action_;
  case Command::Erase:
    return erase_action_;
  case Command::Download:
    return download_action_;
  case Command::Open:
    return open_config_action_;
  case Command::Save:
    return save_config_action_;
  case Command::SaveAs:
    return save_as_config_action_;
  case Command::Reset:
    return reset_config_action_;
  }
  return nullptr;
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
    int erase_sector_maximum = qtmon::roundedSectorCount(external_flash_size, sector_size);
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
                                        qtmon::isDownloadableState(current_state));
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
