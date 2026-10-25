/**
 * @file    mainwindow.cpp
 * @brief   Tag Programmer's window and dialogs.
 *
 * @see     host/applications/qtprogram/README.md,
 *          design/batch-station.md
 */

#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <cstdio>

#include "logwindow.h"
#include "qtfiledialog.h"
#include "qtuserguide.h"

/// Window title and the version shown in About; bump with user-visible changes.
#define title_string "Tag Programmer v2.0"

namespace
{

/// The station panel's row names, in Station::Check order.
const char *kCheckNames[Station::kCheckCount] = {
    QT_TRANSLATE_NOOP("MainWindow", "Programmer"), QT_TRANSLATE_NOOP("MainWindow", "Base"),
    QT_TRANSLATE_NOOP("MainWindow", "SWD"),        QT_TRANSLATE_NOOP("MainWindow", "Supply"),
    QT_TRANSLATE_NOOP("MainWindow", "Tag"),        QT_TRANSLATE_NOOP("MainWindow", "Tag state"),
    QT_TRANSLATE_NOOP("MainWindow", "Firmware"),   QT_TRANSLATE_NOOP("MainWindow", "Record")};

/// The firmware drop-down's item data for "Other ELF file...".
const char *kOtherItem = "other";

/// Colour of a status light.
QString lightColour(Station::Level level)
{
  switch (level) {
  case Station::Level::Ok:
    return QStringLiteral("#2e7d32");
  case Station::Level::Warn:
    return QStringLiteral("#c77700");
  case Station::Level::Fail:
    return QStringLiteral("#c62828");
  default:
    return QStringLiteral("#9e9e9e");
  }
}

/**
 * @brief Saves @p window, frame included, as a PNG at @p path.
 * @return true when the file was written.
 */
bool saveWindowImage(QWidget *window, const QString &path)
{
  QPixmap pixmap;
  if (QScreen *screen = window->windowHandle() ? window->windowHandle()->screen() : nullptr) {
    const QRect frame = window->frameGeometry();
    pixmap = screen->grabWindow(0, frame.x(), frame.y(), frame.width(), frame.height());
  }
  if (pixmap.isNull())
    pixmap = window->grab();
  const bool ok = !pixmap.isNull() && pixmap.save(path, "PNG");
  // To the terminal, not the log pane, which is in the screenshot.
  std::fprintf(stderr, "%s %s\n", ok ? "Saved screenshot" : "Could not save screenshot",
               qPrintable(path));
  return ok;
}

/// Runs the event loop for @p ms, so the window can repaint or a check finish.
void settle(int ms)
{
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  setWindowTitle(QStringLiteral(title_string));
  buildUi();
  HostUserGuide::addHelpMenu(
      this, tr("Tag Programmer"), QStringLiteral("qtprogram"),
      tr("%1\n\nProgram and test a batch of tags through a USB base, recording "
         "each one.").arg(title_string));

  QMenu *stationMenu = menuBar()->addMenu(tr("&Station"));
  autoAttachAction_ = stationMenu->addAction(tr("Attach Automatically"));
  autoAttachAction_->setCheckable(true);
  autoAttachAction_->setChecked(station_.autoAttach());
  connect(autoAttachAction_, &QAction::toggled, &station_, &Station::setAutoAttach);
  stationMenu->addAction(tr("Choose Programmer..."), this, &MainWindow::chooseProgrammer);
  stationMenu->addAction(tr("Choose Base..."), this, [this]() {
    if (!station_.listBases()) {
      QMessageBox::information(this, tr("Choose a Base"),
                               tr("No ST-LINK is on USB, or programming is in progress."));
      return;
    }
    chooseBase();
  });
  stationMenu->addAction(tr("Choose Record File..."), this, &MainWindow::chooseRecord);
  stationMenu->addAction(tr("Refresh Firmware Releases"), this,
                         [this]() { station_.catalog().refresh(); });

  connect(&station_, &Station::checkChanged, this, [this](Station::Check c) {
    showCheck(c);
    updateButtons();
  });
  connect(&station_, &Station::tagChanged, this, [this]() {
    showTag();
    updateButtons();
  });
  connect(&station_, &Station::firmwareChanged, this, &MainWindow::showFirmware);
  connect(&station_, &Station::baseChoiceNeeded, this, &MainWindow::chooseBase);
  connect(&station_, &Station::recordNeeded, this, &MainWindow::chooseRecord);
  connect(&station_, &Station::programFinished, this, &MainWindow::programFinished);
  connect(&station_.session(), &ProgramSession::stepChanged, this, &MainWindow::sessionStep);
  connect(&station_.session(), &ProgramSession::eraseProgress, this, [this](int done, int total) {
    progress_->setRange(0, total > 0 ? total : 0);
    progress_->setValue(done);
  });
  connect(&station_.catalog(), &FirmwareCatalog::refreshFailed, this, [](const QString &why) {
    qWarning().noquote() << "Could not list firmware releases:" << why;
  });

  for (int i = 0; i < Station::kCheckCount; ++i)
    showCheck(static_cast<Station::Check>(i));
  showFirmware();
  showTag();
  showBatch();
  updateButtons();
}

void MainWindow::buildUi()
{
  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);

  // Firmware and tag type.
  auto *choice = new QGridLayout;
  firmwareBox_ = new QComboBox;
  firmwareBox_->setMinimumContentsLength(32);
  notesButton_ = new QPushButton(tr("Release Notes"));
  targetBox_ = new QComboBox;
  targetNote_ = new QLabel;
  choice->addWidget(new QLabel(tr("Firmware")), 0, 0);
  choice->addWidget(firmwareBox_, 0, 1);
  choice->addWidget(notesButton_, 0, 2);
  choice->addWidget(new QLabel(tr("Tag type")), 1, 0);
  choice->addWidget(targetBox_, 1, 1);
  choice->addWidget(targetNote_, 1, 2);
  choice->setColumnStretch(1, 1);
  layout->addLayout(choice);
  connect(firmwareBox_, &QComboBox::activated, this, &MainWindow::firmwareChosen);
  connect(targetBox_, &QComboBox::activated, this, [this](int index) {
    if (!filling_)
      station_.chooseTarget(targetBox_->itemText(index));
  });
  connect(notesButton_, &QPushButton::clicked, this, [this]() {
    QDesktopServices::openUrl(station_.catalog().release(station_.chosenRelease()).pageUrl);
  });

  // Station and tag panels.
  auto *panels = new QHBoxLayout;
  auto *stationBox = new QGroupBox(tr("Station"));
  auto *grid = new QGridLayout(stationBox);
  for (int i = 0; i < Station::kCheckCount; ++i) {
    rows_[i].light = new QLabel(QStringLiteral("●"));
    rows_[i].text = new QLabel;
    rows_[i].text->setWordWrap(true);
    // Room for two lines: a wrapped label does not grow its grid row once
    // the window is laid out. The tooltip holds the whole text regardless.
    rows_[i].text->setMinimumHeight(2 * rows_[i].text->fontMetrics().lineSpacing());
    rows_[i].text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    rows_[i].text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(rows_[i].light, i, 0, Qt::AlignTop);
    grid->addWidget(new QLabel(tr(kCheckNames[i])), i, 1, Qt::AlignTop);
    grid->addWidget(rows_[i].text, i, 2);
  }
  programmerButton_ = new QPushButton(tr("Choose..."));
  recordButton_ = new QPushButton(tr("Choose..."));
  stopButton_ = new QPushButton(tr("Stop"));
  eraseButton_ = new QPushButton(tr("Erase"));
  grid->addWidget(programmerButton_, static_cast<int>(Station::Check::Programmer), 3);
  grid->addWidget(recordButton_, static_cast<int>(Station::Check::Record), 3);
  auto *stateButtons = new QHBoxLayout;
  stateButtons->addWidget(stopButton_);
  stateButtons->addWidget(eraseButton_);
  grid->addLayout(stateButtons, static_cast<int>(Station::Check::TagState), 3);
  grid->setColumnStretch(2, 1);
  connect(programmerButton_, &QPushButton::clicked, this, &MainWindow::chooseProgrammer);
  connect(recordButton_, &QPushButton::clicked, this, &MainWindow::chooseRecord);
  connect(stopButton_, &QPushButton::clicked, &station_, &Station::stopTag);
  connect(eraseButton_, &QPushButton::clicked, this, [this]() {
    QMessageBox box(QMessageBox::Warning, tr("Erase Tag"),
                    tr("Erase the tag's log? Download it with Tag Monitor first; it "
                       "cannot be recovered."),
                    QMessageBox::Cancel, this);
    QPushButton *erase = box.addButton(tr("Erase"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == erase)
      station_.eraseTag();
  });

  auto *tagBox = new QGroupBox(tr("Tag"));
  auto *form = new QFormLayout(tagBox);
  for (QLabel **value : {&labelValue_, &uuidValue_, &stateValue_, &firmwareValue_,
                         &testsValue_, &clockValue_, &resultValue_}) {
    // One line each: a wrapped value's second line is clipped by the form.
    *value = new QLabel;
    (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  form->addRow(tr("Label"), labelValue_);
  form->addRow(tr("UUID"), uuidValue_);
  form->addRow(tr("State"), stateValue_);
  form->addRow(tr("Firmware"), firmwareValue_);
  form->addRow(tr("Tests"), testsValue_);
  clockValue_->setToolTip(tr("The tag's clock minus this computer's, in seconds"));
  syncButton_ = new QPushButton(tr("Sync"));
  syncButton_->setToolTip(tr("Set the tag's clock to this computer's"));
  connect(syncButton_, &QPushButton::clicked, this, [this]() {
    if (!station_.syncClock())
      QMessageBox::warning(this, tr("Sync Clock"),
                           tr("The tag did not accept the clock setting."));
  });
  auto *clockRow = new QHBoxLayout;
  clockRow->setContentsMargins(0, 0, 0, 0);
  clockRow->addWidget(clockValue_, 1);
  clockRow->addWidget(syncButton_);
  form->addRow(tr("Clock"), clockRow);
  form->addRow(tr("Result"), resultValue_);
  panels->addWidget(stationBox, 3);
  panels->addWidget(tagBox, 2);
  layout->addLayout(panels);

  // Actions.
  auto *actions = new QHBoxLayout;
  programButton_ = new QPushButton(tr("Program"));
  programButton_->setDefault(true);
  retestButton_ = new QPushButton(tr("Retest"));
  nextButton_ = new QPushButton(tr("Next Tag"));
  attachButton_ = new QPushButton(tr("Attach"));
  showCsvButton_ = new QPushButton(tr("Show CSV"));
  actions->addWidget(programButton_);
  actions->addWidget(retestButton_);
  actions->addWidget(nextButton_);
  actions->addWidget(attachButton_);
  actions->addStretch();
  actions->addWidget(showCsvButton_);
  layout->addLayout(actions);
  connect(programButton_, &QPushButton::clicked, this, &MainWindow::programClicked);
  connect(retestButton_, &QPushButton::clicked, this, &MainWindow::retestClicked);
  connect(nextButton_, &QPushButton::clicked, &station_, &Station::nextTag);
  connect(attachButton_, &QPushButton::clicked, &station_, &Station::attach);
  connect(showCsvButton_, &QPushButton::clicked, this, [this]() {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(station_.record().path()).absolutePath()));
  });

  // Session progress.
  auto *sessionRow = new QHBoxLayout;
  stepLabel_ = new QLabel;
  stepLabel_->setWordWrap(true);
  progress_ = new QProgressBar;
  progress_->setMaximumWidth(200);
  retryButton_ = new QPushButton(tr("Retry"));
  cancelButton_ = new QPushButton(tr("Give Up"));
  sessionRow->addWidget(stepLabel_, 1);
  sessionRow->addWidget(progress_);
  sessionRow->addWidget(retryButton_);
  sessionRow->addWidget(cancelButton_);
  layout->addLayout(sessionRow);
  progress_->hide();
  retryButton_->hide();
  cancelButton_->hide();
  connect(retryButton_, &QPushButton::clicked, &station_.session(), &ProgramSession::retryConnect);
  connect(cancelButton_, &QPushButton::clicked, &station_.session(), &ProgramSession::cancel);

  // Batch counter and log.
  auto *bottom = new QHBoxLayout;
  batchLabel_ = new QLabel;
  logToggle_ = new QToolButton;
  logToggle_->setText(tr("Log"));
  logToggle_->setCheckable(true);
  logToggle_->setArrowType(Qt::RightArrow);
  logToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  bottom->addWidget(batchLabel_, 1);
  bottom->addWidget(logToggle_);
  layout->addLayout(bottom);
  log_ = new LogWindow;
  log_->hide();
  layout->addWidget(log_, 1);
  connect(logToggle_, &QToolButton::toggled, this, [this](bool on) {
    log_->setVisible(on);
    logToggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    if (!on)
      QTimer::singleShot(0, this, [this]() { adjustSize(); });
  });

  setCentralWidget(central);
  resize(900, 0);
}

void MainWindow::start()
{
  if (!QSettings().contains("resetsAcknowledged")) {
    const auto answer = QMessageBox::question(
        this, tr("Attach Automatically?"),
        tr("Tag Programmer can connect to each tag as soon as it finds the base, "
           "so its state and identity are shown without pressing Attach.\n\n"
           "Connecting resets the tag, which ends a deployment that is running. "
           "Attach automatically from now on? (Station > Attach Automatically "
           "changes this.)"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    // Setting the action records the answer through its toggled() signal.
    station_.setAutoAttach(answer == QMessageBox::Yes);
    autoAttachAction_->setChecked(answer == QMessageBox::Yes);
  }
  station_.begin();
}

void MainWindow::showCheck(Station::Check check)
{
  const Station::CheckStatus s = station_.status(check);
  CheckRow &row = rows_[static_cast<int>(check)];
  row.light->setStyleSheet(QStringLiteral("color: %1;").arg(lightColour(s.level)));
  row.text->setText(s.text.isEmpty() ? QStringLiteral("—") : s.text);
  row.text->setToolTip(s.text);

  using C = Station::Check;
  if (check == C::Programmer)
    programmerButton_->setVisible(s.level != Station::Level::Ok);
  if (check == C::Record) {
    recordButton_->setVisible(s.level != Station::Level::Ok);
    showBatch();
  }
}

void MainWindow::showFirmware()
{
  filling_ = true;
  firmwareBox_->clear();
  int current = -1;
  for (const FirmwareRelease &r : station_.catalog().releases()) {
    QString text = r.tag;
    if (r.published.isValid())
      text += QStringLiteral("  (%1)").arg(r.published.date().toString(Qt::ISODate));
    if (r.prerelease)
      text += tr("  prerelease");
    if (!r.cached)
      text += tr("  not downloaded");
    if (r.tag == station_.chosenRelease())
      current = firmwareBox_->count();
    firmwareBox_->addItem(text, QStringLiteral("release:") + r.tag);
  }
  if (!station_.chosenRelease().isEmpty() && current < 0) {
    current = firmwareBox_->count();
    firmwareBox_->addItem(station_.chosenRelease(),
                          QStringLiteral("release:") + station_.chosenRelease());
  }
  const QStringList recent = station_.recentFiles();
  if (!recent.isEmpty() || !station_.chosenFile().isEmpty())
    firmwareBox_->insertSeparator(firmwareBox_->count());
  QStringList files = recent;
  if (!station_.chosenFile().isEmpty() && !files.contains(station_.chosenFile()))
    files.prepend(station_.chosenFile());
  for (const QString &file : files) {
    if (file == station_.chosenFile())
      current = firmwareBox_->count();
    firmwareBox_->addItem(QFileInfo(file).fileName() + QStringLiteral("  (") +
                              QDir::toNativeSeparators(QFileInfo(file).absolutePath()) + ')',
                          QStringLiteral("file:") + file);
  }
  firmwareBox_->insertSeparator(firmwareBox_->count());
  firmwareBox_->addItem(tr("Other ELF file..."), QString::fromLatin1(kOtherItem));
  if (current < 0) {
    firmwareBox_->insertItem(0, tr("Choose firmware"), QString());
    current = 0;
  }
  firmwareBox_->setCurrentIndex(current);
  notesButton_->setEnabled(
      !station_.catalog().release(station_.chosenRelease()).pageUrl.isEmpty());

  targetBox_->clear();
  const QStringList targets = station_.targets();
  targetBox_->addItems(targets);
  if (!station_.target().isEmpty() && !targets.contains(station_.target()))
    targetBox_->addItem(station_.target());
  targetBox_->setCurrentText(station_.target());
  if (station_.target().isEmpty())
    targetBox_->setCurrentIndex(-1);
  targetBox_->setEnabled(!station_.chosenRelease().isEmpty());
  targetNote_->setText(!station_.chosenFile().isEmpty()
                           ? tr("from the image")
                           : station_.targetDetected() ? tr("detected from the tag")
                                                       : QString());
  filling_ = false;
  updateButtons();
}

void MainWindow::firmwareChosen(int index)
{
  if (filling_)
    return;
  const QString data = firmwareBox_->itemData(index).toString();
  if (data.startsWith("release:")) {
    station_.chooseRelease(data.mid(8));
  } else if (data.startsWith("file:")) {
    station_.chooseFile(data.mid(5));
  } else if (data == QLatin1String(kOtherItem)) {
    const QString last = station_.chosenFile();
    const QString path = HostFileDialog::getOpenFileName(
        this, tr("Firmware image to program"),
        last.isEmpty() ? QDir::homePath() : QFileInfo(last).absolutePath(),
        tr("Firmware ELF (*.elf);;All Files (*)"));
    if (!path.isEmpty())
      station_.chooseFile(path);
    else
      showFirmware();
  }
}

void MainWindow::showTag()
{
  labelValue_->setText(station_.lastLabel());
  if (!station_.tagAnswers()) {
    for (QLabel *l : {uuidValue_, stateValue_, firmwareValue_, testsValue_, clockValue_})
      l->setText(QStringLiteral("—"));
    clockValue_->setStyleSheet(QString());
    return;
  }
  const TagInfo &info = station_.tagInfo();
  const Status &status = station_.tagStatus();
  uuidValue_->setText(QString::fromStdString(info.uuid()));
  stateValue_->setText(QString::fromStdString(TagState_Name(status.state())));
  firmwareValue_->setText(QStringLiteral("%1  %2  (%3)")
                              .arg(station_.tagTarget(),
                                   QString::fromStdString(info.githash()),
                                   QString::fromStdString(info.board_desc())));
  testsValue_->setText(QString::fromStdString(TestResult_Name(status.test_status())));
  const double offset = station_.clockOffset();
  const bool off = qAbs(offset) > Station::kClockToleranceS;
  clockValue_->setText(qAbs(offset) < 1000.0 ? QString::asprintf("%+.2f s", offset)
                                             : QString::asprintf("%+.3g s", offset));
  if (off)
    clockValue_->setText(clockValue_->text() + tr("  (press Sync)"));
  clockValue_->setStyleSheet(
      off ? QStringLiteral("color: %1;").arg(lightColour(Station::Level::Warn)) : QString());
  const QStringList labels = station_.record().labelsFor(QString::fromStdString(info.uuid()));
  if (!labels.isEmpty())
    labelValue_->setText(tr("%1 (recorded)").arg(labels.last()));
}

void MainWindow::showBatch()
{
  batchLabel_->setText(tr("Batch: %1 programmed this session, %2 not OK")
                           .arg(station_.sessionOk())
                           .arg(station_.sessionFailed()));
  showCsvButton_->setEnabled(station_.record().isOpen());
}

void MainWindow::updateButtons()
{
  const bool running = station_.session().running();
  QString why;
  programButton_->setEnabled(station_.canProgram(&why));
  programButton_->setToolTip(programButton_->isEnabled() ? QString() : why);
  retestButton_->setEnabled(!running && station_.tagAnswers() &&
                            station_.status(Station::Check::Record).level ==
                                Station::Level::Ok);
  nextButton_->setEnabled(!running);
  attachButton_->setEnabled(!running && station_.status(Station::Check::Programmer).level ==
                                             Station::Level::Ok);
  firmwareBox_->setEnabled(!running);
  targetBox_->setEnabled(!running && !station_.chosenRelease().isEmpty());
  const TagState s = station_.tagStatus().state();
  stopButton_->setVisible(station_.tagAnswers() && s != IDLE && s != FINISHED && s != ABORTED &&
                          s != TEST && s != sRESET);
  eraseButton_->setVisible(station_.tagAnswers() && (s == FINISHED || s == ABORTED));
  stopButton_->setEnabled(!running);
  syncButton_->setEnabled(!running && station_.tagAnswers());
  eraseButton_->setEnabled(!running);
}

void MainWindow::chooseProgrammer()
{
  // A file, not a folder: the file dialog shows a macOS application bundle
  // as a file, so STM32CubeProgrammer.app can be chosen but not entered.
#ifdef Q_OS_MACOS
  const QString start = QStringLiteral("/Applications");
  const QString caption = tr("Choose STM32CubeProgrammer.app or STM32_Programmer_CLI");
#else
  const QString start = QDir::homePath();
  const QString caption = tr("Choose STM32_Programmer_CLI (in STM32CubeProgrammer's bin folder)");
#endif
  const QString path = HostFileDialog::getOpenFileName(this, caption, start);
  if (path.isEmpty())
    return;
  if (!station_.setProgrammerDirectory(path))
    QMessageBox::warning(this, tr("STM32CubeProgrammer"),
                         tr("STM32_Programmer_CLI is not %1, nor inside it.")
                             .arg(QDir::toNativeSeparators(path)));
}

void MainWindow::chooseBase()
{
  QStringList items;
  int current = 0;
  for (const UsbDev &dev : station_.bases()) {
    const QString serial = QString::fromStdString(dev.serial);
    if (!serial.isEmpty() && serial == station_.baseSerial())
      current = static_cast<int>(items.size());
    items << tr("ST-LINK %1  (bus %2, address %3)")
                 .arg(serial.isEmpty() ? tr("serial unknown") : serial)
                 .arg(dev.bus)
                 .arg(dev.address);
  }
  bool ok = false;
  const QString item = QInputDialog::getItem(
      this, tr("Choose a Base"),
      items.size() > 1 ? tr("Several bases are connected. Use:") : tr("Use:"), items, current,
      false, &ok);
  if (ok)
    station_.chooseBase(static_cast<int>(items.indexOf(item)));
}

void MainWindow::chooseRecord()
{
  const QString path = Station::defaultRecordPath();
  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Record File"));
  box.setText(tr("Tag Programmer records every tag it programs in a CSV file."));
  box.setInformativeText(QFileInfo::exists(path)
                             ? tr("Use the record at %1, or choose another?")
                                   .arg(QDir::toNativeSeparators(path))
                             : tr("Create one at %1, or choose another location?")
                                   .arg(QDir::toNativeSeparators(path)));
  QPushButton *useDefault = box.addButton(QFileInfo::exists(path) ? tr("Use It") : tr("Create"),
                                          QMessageBox::AcceptRole);
  QPushButton *open = box.addButton(tr("Open Existing..."), QMessageBox::ActionRole);
  QPushButton *create = box.addButton(tr("Create Elsewhere..."), QMessageBox::ActionRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(useDefault);
  box.exec();

  QString error;
  bool ok = true;
  if (box.clickedButton() == useDefault) {
    ok = QFileInfo::exists(path) ? station_.openRecord(path, &error)
                                 : station_.createRecord(path, &error);
  } else if (box.clickedButton() == open) {
    const QString chosen = HostFileDialog::getOpenFileName(
        this, tr("Record file"), QFileInfo(path).absolutePath(), tr("CSV (*.csv)"));
    if (!chosen.isEmpty())
      ok = station_.openRecord(chosen, &error);
  } else if (box.clickedButton() == create) {
    const QString chosen = HostFileDialog::getSaveFileName(
        this, tr("New record file"), path, tr("CSV (*.csv)"));
    if (!chosen.isEmpty())
      ok = QFileInfo::exists(chosen) ? station_.openRecord(chosen, &error)
                                     : station_.createRecord(chosen, &error);
  }
  if (!ok)
    QMessageBox::warning(this, tr("Record File"), error);
}

bool MainWindow::confirmProgram(QString &label, const QString &capture)
{
  const FirmwareImage &image = station_.image();
  const bool answers = station_.tagAnswers();
  const int tagType = answers ? station_.tagInfo().tag_type() : image.identity.tagType;
  const bool calibrated = tagType == COMPASSTAG || tagType == IMUTAG;
  const QString bullet = QStringLiteral("• ");
  QStringList lost, kept;
  if (answers)
    lost << tr("The tag's log. Download it first with Tag Monitor.");
  lost << tr("The tag's stored configuration.");
  if (calibrated && image.plan.noLayout)
    lost << tr("Its calibration constants: this image predates the flash layout record, "
               "so all of flash is erased.");
  else if (calibrated && !image.plan.keepsCalibration())
    lost << tr("Its calibration constants: this image keeps no calibration page.");
  if (calibrated && image.plan.keepsCalibration())
    kept << tr("Its calibration constants.");
  kept << tr("The RTC trim and the board UUID.");

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Program Tag"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  auto *labelEdit = new QLineEdit;
  labelEdit->setPlaceholderText(tr("The label on the board, e.g. BitTag-017"));
  form->addRow(tr("Label"), labelEdit);
  const QString uuid = answers ? QString::fromStdString(station_.tagInfo().uuid()) : QString();
  form->addRow(tr("Tag"),
               new QLabel(answers ? tr("%1  %2  UUID %3  %4")
                                        .arg(station_.tagTarget(),
                                             QString::fromStdString(
                                                 TagState_Name(station_.tagStatus().state())),
                                             uuid, station_.status(Station::Check::Supply).text)
                                  : tr("No firmware answers (blank board); %1")
                                        .arg(station_.status(Station::Check::Supply).text)));
  form->addRow(tr("Image"), new QLabel(tr("%1, commit %2\nSHA-256 %3")
                                           .arg(image.displayName(),
                                                image.identity.gitHash.isEmpty()
                                                    ? image.manifestCommit.left(8)
                                                    : image.identity.gitHash,
                                                image.sha256)));
  form->addRow(tr("Erase"), new QLabel(image.plan.summary()));
  form->addRow(tr("Lost"), new QLabel(bullet + lost.join("\n" + bullet)));
  form->addRow(tr("Kept"), new QLabel(bullet + kept.join("\n" + bullet)));
  layout->addLayout(form);

  auto *warning = new QLabel;
  warning->setWordWrap(true);
  warning->setStyleSheet(QStringLiteral("color: %1;").arg(lightColour(Station::Level::Warn)));
  layout->addWidget(warning);
  const QString running = station_.tagTarget();
  const QString mismatch =
      !running.isEmpty() && image.identity.found && running != image.identity.target
          ? tr("The image is for %1, and the tag runs %2. Is it the right image?")
                .arg(image.identity.target, running)
          : QString();
  const QString unqualified = station_.qualificationWarning();

  auto *buttons = new QDialogButtonBox;
  QPushButton *go = buttons->addButton(tr("Erase and Program"), QDialogButtonBox::AcceptRole);
  QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
  cancel->setDefault(true);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto refresh = [&]() {
    const QString text = labelEdit->text().trimmed();
    QStringList notes;
    if (!mismatch.isEmpty())
      notes << mismatch;
    if (!unqualified.isEmpty())
      notes << unqualified;
    const QStringList others = station_.record().uuidsFor(text);
    if (!text.isEmpty() && !others.isEmpty() && !(others.size() == 1 && others.first() == uuid))
      notes << tr("%1 is already recorded for UUID %2.").arg(text, others.join(", "));
    const QStringList labels = station_.record().labelsFor(uuid);
    if (!uuid.isEmpty() && !text.isEmpty() && !labels.isEmpty() && !labels.contains(text))
      notes << tr("This tag is recorded as %1.").arg(labels.join(", "));
    warning->setText(notes.join('\n'));
    warning->setVisible(!notes.isEmpty());
    go->setEnabled(!text.isEmpty());
  };
  connect(labelEdit, &QLineEdit::textChanged, &dialog, refresh);
  labelEdit->setText(label);
  refresh();
  labelEdit->setFocus();

  if (!capture.isEmpty()) {
    labelEdit->setText(QStringLiteral("BitTag-017"));
    QTimer::singleShot(1000, &dialog, [this, &dialog, capture]() {
      saveWindowImage(&dialog, capture);
      dialog.reject();
    });
  }
  if (dialog.exec() != QDialog::Accepted)
    return false;
  label = labelEdit->text().trimmed();
  return true;
}

void MainWindow::programClicked()
{
  QString why;
  if (!station_.canProgram(&why)) {
    QMessageBox::warning(this, tr("Program Tag"), why);
    return;
  }
  QString label;
  if (!confirmProgram(label))
    return;
  labelValue_->setText(label);
  resultValue_->clear();
  station_.program(label);
  updateButtons();
}

void MainWindow::retestClicked()
{
  const QStringList labels =
      station_.record().labelsFor(QString::fromStdString(station_.tagInfo().uuid()));
  bool ok = false;
  const QString label = QInputDialog::getText(
      this, tr("Retest Tag"), tr("Label of the tag to retest:"), QLineEdit::Normal,
      labels.isEmpty() ? QString() : labels.last(), &ok).trimmed();
  if (!ok || label.isEmpty())
    return;
  labelValue_->setText(label);
  resultValue_->clear();
  station_.retest(label);
  updateButtons();
}

void MainWindow::sessionStep(ProgramSession::Step step, const QString &text)
{
  const bool running = step != ProgramSession::Step::Done;
  stepLabel_->setText(running ? text : QString());
  progress_->setVisible(running && step != ProgramSession::Step::PowerCycle);
  progress_->setRange(0, 0);
  retryButton_->setVisible(step == ProgramSession::Step::PowerCycle);
  cancelButton_->setVisible(step == ProgramSession::Step::PowerCycle ||
                            step == ProgramSession::Step::Stopping ||
                            step == ProgramSession::Step::Erasing);
  cancelButton_->setText(step == ProgramSession::Step::PowerCycle ? tr("Give Up")
                                                                  : tr("Cancel"));
  updateButtons();
}

void MainWindow::programFinished(const ProgramRecord &record, bool written,
                                 const QString &message)
{
  const bool ok = written && record.result == QLatin1String("OK");
  resultValue_->setText(record.result.isEmpty() ? message : record.result);
  resultValue_->setStyleSheet(QStringLiteral("color: %1;").arg(
      lightColour(ok ? Station::Level::Ok : Station::Level::Fail)));
  stepLabel_->setText(message);
  showTag();
  showBatch();
  updateButtons();
  if (!ok)
    QMessageBox::warning(this, tr("Program Tag"), message);
}

void MainWindow::captureScreenshots(const QString &dir)
{
  QDir().mkpath(dir);
  raise();
  activateWindow();
  // Let the checks settle, including an automatic attach and a download.
  for (int waited = 0; waited < 60000 && station_.busy(); waited += 500)
    settle(500);
  settle(1000);
  bool ok = saveWindowImage(this, QDir(dir).filePath("qtprogram-station.png"));
  if (station_.image().usable()) {
    QString label;
    confirmProgram(label, QDir(dir).filePath("qtprogram-program.png"));
  } else {
    std::fprintf(stderr, "No usable firmware chosen; the Program dialog was not captured.\n");
    ok = false;
  }
  qApp->exit(ok ? 0 : 1);
}

void MainWindow::benchProgram(const QString &label)
{
  for (int waited = 0; waited < 60000 && station_.busy(); waited += 500)
    settle(500);
  settle(500);
  for (int i = 0; i < Station::kCheckCount; ++i) {
    const Station::CheckStatus s = station_.status(static_cast<Station::Check>(i));
    std::printf("%-10s %-5s %s\n", kCheckNames[i],
                s.level == Station::Level::Ok     ? "ok"
                : s.level == Station::Level::Warn ? "warn"
                : s.level == Station::Level::Fail ? "FAIL"
                                                  : "-",
                qPrintable(s.text));
  }
  QString why;
  if (!station_.canProgram(&why)) {
    std::printf("Cannot program: %s\n", qPrintable(why));
    qApp->exit(2);
    return;
  }
  connect(&station_.session(), &ProgramSession::stepChanged, this,
          [this](ProgramSession::Step step, const QString &text) {
            std::printf("[%s] %s\n", qPrintable(QDateTime::currentDateTime().toString("HH:mm:ss")),
                        qPrintable(text));
            std::fflush(stdout);
            if (step == ProgramSession::Step::PowerCycle) {
              // Retry every 5 s for 3 minutes while someone replugs the base.
              auto *retry = new QTimer(this);
              connect(retry, &QTimer::timeout, this, [this, retry, left = 36]() mutable {
                if (station_.session().step() != ProgramSession::Step::PowerCycle)
                  retry->deleteLater();
                else if (--left <= 0)
                  station_.session().cancel();
                else
                  station_.session().retryConnect();
              });
              retry->start(5000);
            }
          });
  connect(&station_, &Station::programFinished, this,
          [](const ProgramRecord &row, bool written, const QString &) {
            std::printf("Recorded: %s\n%s\n", written ? "yes" : "NO",
                        qPrintable(row.fields().join(',')));
            qApp->exit(written && row.result == QLatin1String("OK") ? 0 : 1);
          });
  station_.program(label);
}
