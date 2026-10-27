/**
 * @file    mainwindow.cpp
 * @brief   qtmonitor's main window: commands sent to the tag session, the
 *          session's reports fed to the model, and the model drawn.
 *
 * @details The maintainer-only screenshot capture and the action wiring
 *          check are in mainwindow_fixture.cpp. The window holds no tag and
 *          no tag state of its own: see mainwindow.h and
 *          design/architecture.md.
 */

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "configshape.h"
#include "fixturesession.h"
#include "livetagsession.h"
#include "qtfiledialog.h"
#include "qtuserguide.h"
#include "startchecks.h"
#include "tagconfiguration/configtab.h"
#include "taglogwriter.h"
#include "tagrules.h"
#include "tagstatusstrip.h"

using qtmon::Command;

namespace
{

/// The lowest qtmonitor version a tag may require; see onAttached().
constexpr float kRequiredMonitorVersion = 2.0f;

/**
 * @brief Shows a warning without waiting for it to be dismissed.
 *
 * @details A modal exec() would run a nested event loop, in which the next
 *          status could arrive and re-enter the window; open() returns at
 *          once and the box deletes itself when closed.
 *
 * @param[in] parent       The window.
 * @param[in] title        Window title.
 * @param[in] text         The main sentence.
 * @param[in] informative  Further text, or empty.
 * @param[in] detailed     Text behind Show Details, or empty.
 * @param[in] icon         The box's icon.
 */
void showMessage(QWidget *parent, const QString &title, const QString &text,
                 const QString &informative = QString(), const QString &detailed = QString(),
                 QMessageBox::Icon icon = QMessageBox::Warning)
{
  auto *box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  if (!informative.isEmpty())
    box->setInformativeText(informative);
  if (!detailed.isEmpty())
    box->setDetailedText(detailed);
  box->open();
}

/// A TagState's name, as the tag reports it.
QString stateName(TagState state)
{
  return QString::fromStdString(std::string(TagState_Name(state)));
}

} // namespace

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
  captureStartupScreenshotOnStartup = options.captureStartupScreenshot;
  captureMainScreenshotsOnStartup = options.captureMainScreenshots;
  captureConfigScreenshotsOnStartup = options.captureConfigScreenshots;
  captureErrorLogScreenshotOnStartup = options.captureErrorLogScreenshot;

  ui.mainTabWidget->setCurrentIndex(0);
  updateTitle();

  // A fixture replays a captured tag through the same window, so screenshots
  // show exactly what a user sees; otherwise the worker thread owns a real tag.
  if (!options.fakeFixturePath.isEmpty()) {
    auto fixture = std::make_unique<FixtureSession>();
    fixture_ = fixture.get();
    const bool loaded = fixture->load(options.fakeFixturePath);
    session_ = std::move(fixture);
    connectSession();
    if (loaded) {
      ui.errorTab->setEnabled(false);
      session_->attach(UsbDev());
      fixture_->showState(options.fakeState);
    }
  } else {
    session_ = std::make_unique<LiveTagSession>();
    connectSession();
    // A failed status read is skipped silently; this redraw is what notices
    // that none has arrived for a while.
    silence_timer_ = new QTimer(this);
    silence_timer_->setInterval(1000);
    connect(silence_timer_, &QTimer::timeout, this, [this]() {
      if (model_.attached())
        render();
    });
    silence_timer_->start();
    if (!options.skipAutoAttach)
      attachTag();
  }
  render();
}

MainWindow::~MainWindow() = default;

void MainWindow::connectSession()
{
  TagSession *s = session_.get();
  connect(s, &TagSession::basesFound, this, &MainWindow::onBasesFound);
  connect(s, &TagSession::attached, this, &MainWindow::onAttached);
  connect(s, &TagSession::attachFailed, this, &MainWindow::onAttachFailed);
  connect(s, &TagSession::detached, this, &MainWindow::onDetached);
  connect(s, &TagSession::statusRead, this, &MainWindow::onStatus);
  connect(s, &TagSession::configRead, this, &MainWindow::onConfigRead);
  connect(s, &TagSession::startSent, this, &MainWindow::onStartSent);
  connect(s, &TagSession::testSent, this, &MainWindow::onTestSent);
  connect(s, &TagSession::eraseSent, this, &MainWindow::onEraseSent);
  connect(s, &TagSession::commandFailed, this, &MainWindow::onCommandFailed);
  connect(s, &TagSession::downloadProgress, this, &MainWindow::onDownloadProgress);
  connect(s, &TagSession::downloadFinished, this, &MainWindow::onDownloadFinished);
}

/********************************************
 *        Drawing the model
 ********************************************/

void MainWindow::render()
{
  const bool attached = model_.attached();
  ui.TagInformation->setEnabled(attached);
  bool downloadable = false;
  if (!attached) {
    status_strip_->setDetached();
    ui.configtab->Detach();
  } else if (model_.hasStatus()) {
    const Status &status = model_.status();
    ui.internalCount->setText(QString::number(status.internal_data_count()));
    if (status.external_data_count() != 0) {
      ui.externalCount->setText(QString::number(status.external_data_count()));
      ui.ExternalLog->setVisible(true);
    } else {
      ui.ExternalLog->setVisible(false);
    }
    status_strip_->setStatus(status.state(), model_.clockOffsetS(), model_.voltage(),
                             model_.lastTest());
    if (model_.testPending())
      status_strip_->setTestRunning();
    status_strip_->setNoAnswer(static_cast<int>(silenceS()));
    downloadable = qtmon::isDownloadableState(status.state()) && !model_.downloading();
    // The Configuration tab locks a running tag's own configuration.
    ui.configtab->StateUpdate(status.state());
  }
  ui.datadownloadgroupBox->setEnabled(downloadable);

  if (erase_progress_ && model_.erasing()) {
    if (model_.eraseMaximum() > erase_progress_->maximum())
      erase_progress_->setRange(0, model_.eraseMaximum());
    erase_progress_->setValue(model_.eraseValue());
    const qint64 silent = silenceS();
    erase_progress_->setLabelText(
        silent > 0 ? tr("Erasing flash... (no answer from the tag for %1 s)").arg(silent)
                   : tr("Erasing flash..."));
  }
  updateActions();
}

void MainWindow::populateTagInfo(const TagInfo &info)
{
  ui.info_tagtype->setText(QString::fromStdString(TagType_Name(info.tag_type())));
  ui.info_boardname->setText(QString::fromStdString(info.board_desc()));
  ui.info_firmware->setText(QString::fromStdString(info.firmware()));
  ui.info_gitHash->setText(QString::fromStdString(info.githash()));
  ui.info_gitUrl->setText(QString::fromStdString(info.gitrepo()));
  ui.info_uuid->setText(QString::fromStdString(info.uuid()));
  ui.info_flash->setText(QString::number(info.intflashsz()) + "KB");
  ui.info_flash_ext->setText(QString::number(info.extflashsz() / (1024 * 1024)) + "MB");
  ui.info_buildDate->setText(QString::fromStdString(info.build_time()));
  ui.info_srcpath->setText(QString::fromStdString(info.source_path()));
  ui.configtab->SetAttachedTagType(info.tag_type());
  status_strip_->setAttached(info.tag_type(), QString::fromStdString(info.uuid()));
}

void MainWindow::attachConfig(const Config &config)
{
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

qint64 MainWindow::silenceS() const
{
  if (!silence_timer_)
    return 0;  // a fixture answers only when asked to
  const std::int64_t ms = model_.silenceMs(QDateTime::currentMSecsSinceEpoch());
  return ms >= qtmon::kNoAnswerMs ? ms / 1000 : 0;
}

QString MainWindow::tagName() const
{
  return QString("%1 %2").arg(
      QString::fromStdString(std::string(TagType_Name(model_.info().tag_type()))),
      QString::fromStdString(model_.info().uuid()));
}

/********************************************
 *        Session reports
 ********************************************/

void MainWindow::onBasesFound(std::vector<UsbDev> bases)
{
  if (bases.empty()) {
    showMessage(this, tr("Warning"), tr("No Tag Bases Found"));
    render();
    return;
  }

  size_t index = 0;
  if (bases.size() > 1) {
    QStringList items;
    for (const UsbDev &dev : bases) {
      const QString bus = QString::number(dev.bus).rightJustified(3, '0');
      const QString address = QString::number(dev.address).rightJustified(3, '0');
      const QString vid = QString::number(dev.vid, 16);
      const QString pid = QString::number(dev.pid, 16);
      items << QString("%1:%2    0x%3:0x%4").arg(bus, address, vid, pid);
    }
    QInputDialog dialog(this);
    dialog.setOption(QInputDialog::UseListViewForComboBoxItems);
    dialog.setComboBoxItems(items);
    dialog.setWindowTitle(tr("Available Bases"));
    dialog.setLabelText(tr("Please select a base"));
    if (!dialog.exec())
      return;
    const int chosen = items.indexOf(dialog.textValue());
    index = chosen >= 0 ? static_cast<size_t>(chosen) : 0;
  }
  session_->attach(bases[index]);
}

void MainWindow::onAttached(TagInfo info, Config config)
{
  // The tag can name the oldest qtmonitor it works with; the firmware does
  // not set it yet (TagInfo.qtmonitor_min_version), so a fixed floor stands in.
  if (kRequiredMonitorVersion > version) {
    showMessage(this, tr("Warning"),
                tr("monitor version %1 less than required version %2")
                    .arg(version)
                    .arg(kRequiredMonitorVersion));
    session_->detach();
    return;
  }
  model_.attached(info);
  populateTagInfo(info);
  attachConfig(config);
  render();
}

void MainWindow::onAttachFailed(QString message)
{
  showMessage(this, tr("Warning"), message);
  render();
}

void MainWindow::onDetached()
{
  model_.detached();
  if (erase_progress_)
    erase_progress_->close();
  render();
}

void MainWindow::onStatus(Status status, float voltage, qint64 host_ms)
{
  const std::vector<qtmon::MonitorEvent> events = model_.applyStatus(status, voltage, host_ms);
  render();
  for (const qtmon::MonitorEvent &event : events) {
    const QString text = QString::fromStdString(event.text);
    switch (event.kind) {
    case qtmon::MonitorEvent::Kind::TagLogLine:
      qDebug().noquote() << "Log:" << text;
      break;
    case qtmon::MonitorEvent::Kind::TestFinished:
      qInfo().noquote() << "Self-test on" << tagName() << "finished:"
                        << QString::fromStdString(std::string(TestResult_Name(event.test)));
      break;
    case qtmon::MonitorEvent::Kind::TestTimedOut:
      qWarning().noquote() << "Self-test on" << tagName() << "gave no result within"
                           << qtmon::kTestTimeoutMs / 1000 << "s";
      break;
    case qtmon::MonitorEvent::Kind::StartSucceeded:
      // The tag started although the call reported an error: note the
      // overcome error for anyone reading the log.
      if (!text.isEmpty())
        qWarning().noquote() << "Start() reported a transient failure but the tag reached"
                             << stateName(event.state) << ":" << text;
      break;
    case qtmon::MonitorEvent::Kind::StartFailed: {
      const QString state = tr("Last known state: %1").arg(stateName(event.state));
      if (text.isEmpty())
        showMessage(this, tr("Error"), tr("Start Failed"), state);
      else
        showMessage(this, tr("Error"), tr("Start Failed"), text + "\n" + state, text);
      break;
    }
    case qtmon::MonitorEvent::Kind::EraseFinished:
      if (erase_progress_)
        erase_progress_->close();
      break;
    }
  }
}

void MainWindow::onConfigRead(Config config)
{
  ui.configtab->SetConfig(config);
}

void MainWindow::onStartSent(QString call_message)
{
  model_.startSent(QDateTime::currentMSecsSinceEpoch(), call_message.toStdString());
  render();
}

void MainWindow::onTestSent()
{
  // The model has awaited the result since Self-Test was chosen (testTag()).
}

void MainWindow::onEraseSent()
{
  model_.eraseSent();
  auto *progress = new QProgressDialog(tr("Erasing flash..."), tr("Close"), 0,
                                       model_.eraseMaximum(), this);
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(0);
  // Close only hides the progress; the erase continues on the tag.
  connect(progress, &QProgressDialog::canceled, progress, &QProgressDialog::close);
  erase_progress_ = progress;
  progress->show();
  render();
}

void MainWindow::onCommandFailed(Command command, QString message)
{
  switch (command) {
  case Command::Read:
    showMessage(this, tr("Error"), message);
    break;
  case Command::Sync:
    showMessage(this, tr("Sync Failed"), tr("The tag did not accept the clock sync command."),
                message, message);
    break;
  case Command::Stop:
    showMessage(this, tr("Stop Failed"), message);
    break;
  case Command::Test:
    model_.testRefused();
    qWarning().noquote() << "Self-test on" << tagName() << "was refused:" << message;
    showMessage(this, tr("Self-Test Failed"), message);
    break;
  case Command::Erase:
    showMessage(this, tr("Erase Failed"), message);
    break;
  default:
    showMessage(this, tr("Warning"), message);
    break;
  }
  render();
}

void MainWindow::onDownloadProgress(int value, int maximum)
{
  if (!download_progress_)
    return;
  if (download_progress_->maximum() != maximum)
    download_progress_->setRange(0, maximum);
  download_progress_->setValue(value);
}

void MainWindow::onDownloadFinished(bool ok, QString error)
{
  model_.downloadEnded();
  if (download_progress_)
    download_progress_->close();
  if (!ok)
    showMessage(this, tr("Download Failed"), error, QString(), QString(),
                QMessageBox::Critical);
  render();
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
  start_action_ = make(tr("Start"), tr("Write the configuration to the tag and start it"),
                       &MainWindow::startWithChecks);
  read_config_action_ = make(tr("Read Configuration"), tr("Read the tag's configuration"),
                             &MainWindow::readConfig);

  // The document commands belong to the Configuration tab.
  ConfigTab *config = ui.configtab;
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
  for (qtmon::StartCheck check : qtmon::startCheckFailures(
           model_.clockOffsetS(), model_.lastTest(), ui.configtab->isApproved())) {
    switch (check) {
    case qtmon::StartCheck::Clock:
      failures << tr("Clock is %1 s off (limit %2 s).")
                      .arg(QString::number(model_.clockOffsetS(), 'g', 3))
                      .arg(qtmon::kClockToleranceS);
      break;
    case qtmon::StartCheck::SelfTest:
      failures << tr("Self-test is %1.")
                      .arg(QString::fromStdString(std::string(TestResult_Name(model_.lastTest()))));
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
    box.setInformativeText(QStringLiteral("• ") +
                           failures.join(QStringLiteral("\n• ")));
    QPushButton *anyway = box.addButton(tr("Start Anyway"), QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() != anyway)
      return;
    qWarning().noquote() << "Starting" << tagName() << "although:" << failures.join(" ");
  }
  Config config;
  if (!ui.configtab->GetConfig(config)) {
    qDebug() << "Start: the configuration cannot be written";
    return;
  }
  session_->start(config);
}

void MainWindow::updateTitle()
{
  QString title = tr("Tag Monitor v%1").arg(version);
  const QString name = ui.configtab->documentName();
  if (!name.isEmpty())
    title += QStringLiteral(" — ") + name + QStringLiteral("[*]");
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
  applyCommandInputs(
      model_.commandInputs(ui.configtab->hasDocument(), ui.configtab->documentTagType()));
  status_strip_->setApprovalAvailable(ui.configtab->hasDocument());
}

void MainWindow::applyCommandInputs(const qtmon::CommandInputs &in)
{
  for (Command command : qtmon::kAllCommands) {
    QAction *action = actionFor(command);
    const qtmon::Availability a = qtmon::availability(command, in);
    action->setVisible(a != qtmon::Availability::Hidden);
    action->setEnabled(a == qtmon::Availability::Enabled);
  }
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
 *        Commands
 ********************************************/

void MainWindow::attachTag()
{
  if (model_.attached()) {
    showMessage(this, tr("Warning"), tr("Already Attached to Tag"));
    return;
  }
  session_->findBases();
}

void MainWindow::detachTag()
{
  session_->detach();
}

void MainWindow::readConfig()
{
  if (!ui.configtab->confirmDiscard())
    return;
  session_->readConfig();
}

void MainWindow::syncClock()
{
  session_->syncClock();
}

void MainWindow::stopTag()
{
  session_->stop();
}

void MainWindow::testTag()
{
  if (model_.state() != IDLE)
    return;
  session_->selfTest();
  model_.testSent(QDateTime::currentMSecsSinceEpoch());
  qInfo().noquote() << "Self-test started on" << tagName();
  render();
}

void MainWindow::eraseTag()
{
  QMessageBox box(this);
  box.setWindowTitle(tr("Reset Tag"));
  box.setText(tr("Erase tag state and data ?"));
  box.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  box.setDefaultButton(QMessageBox::Cancel);
  if (box.exec() == QMessageBox::Ok)
    session_->erase();
}

void MainWindow::downloadData()
{
  const TagType tag_type = model_.info().tag_type();
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
  for (TagLogStorageFormat format : formats)
    filter_list << QString::fromStdString(tagLogFileFilter(format));
  QString selected_filter;
  const QString filter = filter_list.join(";;");
  const QString initial_path =
      QDir::homePath() + "/untitled" +
      QString::fromStdString(defaultTagLogExtension(storage_format));

  QString fileName = HostFileDialog::getSaveFileName(this, tr("Save File"), initial_path,
                                                     filter, &selected_filter);
  if (fileName.isEmpty())
    return;

  TagLogStorageFormat selected_storage_format = storage_format;
  for (TagLogStorageFormat format : formats) {
    if (selected_filter == QString::fromStdString(tagLogFileFilter(format))) {
      selected_storage_format = format;
      break;
    }
  }

  // Give the file the chosen format's extension when it has none, or
  // another supported format's.
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
  } else if (suffix_matches_supported_format &&
             file_info.suffix().compare(selected_suffix, Qt::CaseInsensitive) != 0) {
    fileName = file_info.path() + "/" + file_info.completeBaseName() + selected_extension;
  }

  auto *progress = new QProgressDialog(tr("Downloading .."), tr("Cancel"), 0, 0, this);
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(0);
  // Cancel asks the worker to stop after the current page; the dialog closes
  // when the download reports that it has finished.
  connect(progress, &QProgressDialog::canceled, this,
          [this]() { session_->cancelDownload(); });
  download_progress_ = progress;

  model_.downloadStarted();
  render();
  session_->download(fileName, selected_storage_format);
  progress->show();
}
