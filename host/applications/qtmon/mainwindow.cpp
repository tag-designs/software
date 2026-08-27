
#include <QMainWindow>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QScreen>
#include <QSize>
#include <QTime>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QPromise>

#include <ctime>
#include <fstream>
#include <iomanip>
#include <utility>

#include <google/protobuf/util/json_util.h>
#include <streambuf>


#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qtfiledialog.h"

#include "tag.pb.h"

#include "tagconfiguration/configtab.h"
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

/**
 * @brief Resolves fixture paths supplied relative to the source tree.
 *
 * @details Maintainer screenshot commands are usually launched from the build
 *          tree, but fixtures live in the repository. Existing paths and
 *          absolute paths are returned unchanged; otherwise the path is tried
 *          relative to @c TAG_DESIGNS_SOURCE_DIR.
 *
 * @param[in] path   User-supplied fixture or config path.
 *
 * @return The existing absolute/relative path when found, the source-tree
 *         path when that exists, or the original path for caller diagnostics.
 */
QString resolveSourceRelativePath(const QString &path)
{
  const QFileInfo requested(path);
  if (requested.exists() || requested.isAbsolute()) {
    return requested.filePath();
  }

  const QFileInfo sourceRelative(
      QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path));
  if (sourceRelative.exists()) {
    return sourceRelative.filePath();
  }

  return requested.filePath();
}

/**
 * @brief Resolves the directory where generated screenshots should be written.
 *
 * @details An empty path uses the checked-in user-documentation image
 *          directory. Relative override paths are interpreted relative to the
 *          source tree so repeated runs from the build tree update the same
 *          documentation assets.
 *
 * @param[in] path   Optional command-line screenshot directory.
 *
 * @return Absolute or source-tree-relative directory path for screenshot files.
 */
QString resolveScreenshotDir(const QString &path)
{
  if (path.isEmpty()) {
    return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR))
        .filePath("host/docs/src/images");
  }

  const QFileInfo requested(path);
  if (requested.isAbsolute()) {
    return requested.filePath();
  }

  return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path);
}

/**
 * @brief Serializes one Qt JSON object for protobuf JSON parsing.
 *
 * @param[in] object   Fixture sub-object containing protobuf JSON.
 *
 * @return Compact UTF-8 JSON text suitable for JsonStringToMessage().
 */
std::string jsonObjectString(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}

/**
 * @brief Parses one fixture JSON object into a protobuf message.
 *
 * @details The fixture files store captured monitor responses as protobuf JSON.
 *          This helper centralizes error logging so the replay path reports
 *          whether tag info, config, or a named status failed to parse.
 *
 * @param[in] object    Fixture JSON object to parse.
 * @param[out] message  Destination protobuf message.
 * @param[in] context   Human-readable parse context for diagnostics.
 *
 * @return true if parsing succeeded; false if protobuf JSON validation failed.
 */
template <typename Message>
bool parseJsonMessage(const QJsonObject &object, Message &message,
                      const QString &context)
{
  google::protobuf::util::JsonParseOptions options;
  const auto status =
      google::protobuf::util::JsonStringToMessage(jsonObjectString(object),
                                                  &message, options);
  if (!status.ok()) {
    qWarning().noquote() << "Could not parse" << context
                         << QString::fromStdString(status.ToString());
    return false;
  }
  return true;
}

/**
 * @brief Allows pending Qt paint/layout events to complete before capture.
 *
 * @param[in] milliseconds   Minimum event-loop delay before the caller grabs
 *                           the window contents.
 *
 * @warning This spins a local event loop and is intended only for screenshot
 *          automation, not for interactive command handling.
 */
void waitForGuiToSettle(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

/**
 * @brief Converts a fixture id or tag type name into a stable filename token.
 *
 * @param[in] value   Fixture id, label, or tag type name.
 *
 * @return Lower-case token with spaces and underscores mapped to hyphens.
 */
QString safeFileId(QString value)
{
  value = value.toLower();
  value.replace('_', '-');
  value.replace(' ', '-');
  return value;
}

} // namespace

/**
 * @brief Creates the main window and chooses live or fixture replay startup.
 *
 * @details With default options the constructor follows normal qtmonitor
 *          behavior by probing USB and attaching to a live tag. Fixture options
 *          load captured protobuf JSON, populate the same widgets, and avoid
 *          starting the live status timer.
 *
 * @param[in] options   Startup and screenshot-capture behavior.
 * @param[in] parent    Optional Qt parent widget.
 */
MainWindow::MainWindow(const MainWindowOptions &options, QWidget *parent)
    : QMainWindow(parent) {
  ui.setupUi(this);
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);
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
    on_Attach_clicked();
  }

}

MainWindow::~MainWindow()
{
  ui.configtab->Detach();
  if (!fakeMode) {
    tag.Detach();
  }
}

/**
 * @brief Reports whether a one-shot screenshot capture was requested.
 *
 * @return true if main.cpp should schedule a capture slot and exit afterward;
 *         false for interactive qtmonitor sessions.
 */
bool MainWindow::shouldQuitAfterStartup() const
{
  return captureStartupScreenshotOnStartup || captureMainScreenshotsOnStartup
      || captureConfigScreenshotsOnStartup || captureErrorLogScreenshotOnStartup;
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
 *          State-specific buttons are refined later by applyStatus().
 *
 * @param[in] enabled   true when a live or fake tag is considered attached.
 */
void MainWindow::setAttachedUiEnabled(bool enabled)
{
  ui.StatusGroup->setEnabled(enabled);
  ui.TagInformation->setEnabled(enabled);
  ui.ControlGroup->setEnabled(enabled);
  ui.Attach->setEnabled(!enabled);
  ui.Detach->setEnabled(enabled);
  ui.datadownloadgroupBox->setEnabled(false);
  if (!enabled) {
    ui.calibrateButton->setVisible(false);
  }
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
  ui.info_Voltage->setText(QString::number(static_cast<double>(voltage), 'f', 2));
  ui.State->setText(QString::fromStdString(TagState_Name(status.state())));
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
  ui.timeError->setText(QString::number(timeerr / 1000.0, 'f', 2));
  ui.info_testStatus->setText(
      QString::fromStdString(TestResult_Name(status.test_status())));

  ui.syncButton->setEnabled(status.state() == IDLE);
  ui.testButton->setEnabled(status.state() == IDLE);
  ui.calibrateButton->setVisible(tagSupportsCalibration(tag_type));
  ui.calibrateButton->setEnabled(tagSupportsCalibration(tag_type)
                                 && status.state() == IDLE);
  ui.eraseButton->setEnabled((status.state() == FINISHED)
                             || (status.state() == ABORTED));
  ui.datadownloadgroupBox->setEnabled(isDownloadableState(status.state()));
  ui.stopButton->setEnabled((status.state() == CONFIGURED)
                            || (status.state() == RUNNING)
                            || (status.state() == HIBERNATING)
                            || (status.state() == CALIBRATE));

  current_state = status.state();
  emit StateUpdate(current_state);
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

/**
 * @brief Loads a qtmonitor fake-tag fixture from protobuf JSON.
 *
 * @details The fixture supplies TagInfo, Config, voltage, and one or more
 *          named Status messages. Inline config is preferred; a `$ref` config
 *          path is accepted for fixture files that store defaults externally.
 *
 * @param[in] path   Fixture path from the command line, absolute or
 *                   source-tree relative.
 *
 * @return true if all required fixture sections parsed; false otherwise.
 */
bool MainWindow::loadFakeFixture(const QString &path)
{
  const QString resolvedPath = resolveSourceRelativePath(path);
  QFile file(resolvedPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Unable to open qtmonitor fake fixture" << path
               << "resolved as" << resolvedPath;
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    qWarning() << "Unable to parse qtmonitor fake fixture" << resolvedPath
               << parseError.errorString();
    return false;
  }

  const QJsonObject root = document.object();
  fakeFixtureId = root.value("id").toString("fixture");
  fakeVoltage = static_cast<float>(root.value("voltage").toDouble(0.0));
  if (!parseJsonMessage(root.value("info").toObject(), fakeInfo, "fixture info")) {
    return false;
  }

  const QJsonObject configObject = root.value("config").toObject();
  if (configObject.contains("value")) {
    fakeConfigLoaded =
        parseJsonMessage(configObject.value("value").toObject(), fakeConfig,
                         "fixture config");
  } else if (configObject.contains("$ref")) {
    QFile configFile(resolveSourceRelativePath(configObject.value("$ref").toString()));
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "Unable to open fixture config ref" << configFile.fileName();
      return false;
    }
    QJsonParseError configError;
    const QJsonDocument configDocument =
        QJsonDocument::fromJson(configFile.readAll(), &configError);
    if (configError.error != QJsonParseError::NoError || !configDocument.isObject()) {
      qWarning() << "Unable to parse fixture config ref" << configError.errorString();
      return false;
    }
    fakeConfigLoaded =
        parseJsonMessage(configDocument.object(), fakeConfig, "fixture config ref");
  }

  if (!fakeConfigLoaded) {
    qWarning() << "qtmonitor fake fixture has no usable config";
    return false;
  }

  fakeStatuses.clear();
  const QJsonObject statuses = root.value("statuses").toObject();
  for (auto it = statuses.begin(); it != statuses.end(); ++it) {
    Status status;
    if (parseJsonMessage(it.value().toObject(), status,
                         QString("fixture status %1").arg(it.key()))) {
      fakeStatuses.insert(it.key().toLower(), status);
    }
  }
  if (fakeStatuses.isEmpty()) {
    qWarning() << "qtmonitor fake fixture has no statuses";
    return false;
  }

  qInfo() << "Loaded qtmonitor fake fixture" << resolvedPath;
  return true;
}

/**
 * @brief Applies already-loaded fixture metadata to the qtmonitor widgets.
 *
 * @details This switches the window into fake mode, stops live polling, fills
 *          tag information and configuration, and disables the Error Log tab
 *          because it depends on a live Tag attachment.
 *
 * @return true after the fixture state has been applied.
 *
 * @pre loadFakeFixture() has populated fakeInfo, fakeConfig, and statuses.
 */
bool MainWindow::setupFakeFixture()
{
  fakeMode = true;
  timer.stop();
  populateTagInfo(fakeInfo);
  attachConfig(fakeConfig, true);
  ui.errorTab->setEnabled(false);
  setAttachedUiEnabled(true);
  return true;
}

/**
 * @brief Selects or derives a fixture status for screenshot replay.
 *
 * @details If the requested state is not present in the fixture, the idle or
 *          current status is reused and adjusted for display. This keeps early
 *          fixtures useful while allowing future captures to provide real
 *          running or finished Status samples.
 *
 * @param[in] name   Requested fixture status name.
 *
 * @return Status message suitable for applyStatus().
 */
Status MainWindow::fakeStatusForName(const QString &name) const
{
  const QString key = name.toLower();
  Status status;
  if (fakeStatuses.contains(key)) {
    status = fakeStatuses.value(key);
  } else if (fakeStatuses.contains("idle")) {
    status = fakeStatuses.value("idle");
  } else if (fakeStatuses.contains("current")) {
    status = fakeStatuses.value("current");
  } else {
    status = fakeStatuses.first();
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  status.set_millis(now);
  if (key == "idle") {
    status.set_state(IDLE);
    status.set_internal_data_count(0);
    status.set_external_data_count(0);
  } else if (key == "running") {
    status.set_state(RUNNING);
    if (status.internal_data_count() == 0) {
      status.set_internal_data_count(42);
    }
    if (status.external_data_count() == 0) {
      status.set_external_data_count(7);
    }
  } else if (key == "finished") {
    status.set_state(FINISHED);
    if (status.internal_data_count() == 0) {
      status.set_internal_data_count(128);
    }
    if (status.external_data_count() == 0) {
      status.set_external_data_count(512);
    }
  }
  if (status.test_status() == TEST_UNSPECIFIED) {
    status.set_test_status(ALL_PASSED);
  }
  return status;
}

/**
 * @brief Renders one named fixture status in the live qtmonitor widgets.
 *
 * @param[in] name   Fixture status name such as idle, running, or finished.
 *
 * @return true if fake mode was active and the status was applied; false if no
 *         fixture is loaded.
 */
bool MainWindow::applyFakeState(const QString &name)
{
  if (!fakeMode) {
    qWarning() << "qtmonitor fake state requested without a fake fixture";
    return false;
  }
  applyStatus(fakeStatusForName(name), fakeVoltage);
  return true;
}

/**
 * @brief Builds the output path for one generated screenshot filename.
 *
 * @param[in] filename   Basename for the PNG file to write.
 *
 * @return Path under the resolved screenshot directory.
 */
QString MainWindow::screenshotPath(const QString &filename) const
{
  return QDir(screenshotDir).filePath(filename);
}

/**
 * @brief Captures the current window frame into a PNG file.
 *
 * @details The helper first lets Qt compute the natural size with
 *          adjustSize(), using the active widgets' sizeHint() and
 *          minimumSizeHint(). The optional maximums are documentation
 *          guardrails for tabs whose designer geometry or expanding child
 *          widgets request more blank area than is useful in the guide. The
 *          native-screen grab includes the window frame when the platform
 *          allows it, which avoids the clipped widget-grab screenshots that
 *          prompted this hook. Widget capture is retained as a fallback.
 *
 * @param[in] path               Destination PNG path.
 * @param[in] maximumWindowSize  Optional maximum client-area size after Qt
 *                               computes the natural window size.
 * @param[in] maximumImageSize   Optional maximum saved pixel size. Used only
 *                               when the native frame still contains excess
 *                               blank layout space.
 *
 * @return true if a non-null pixmap was saved; false on capture or write error.
 */
bool MainWindow::saveCurrentScreenshot(const QString &path,
                                       const QSize &maximumWindowSize,
                                       const QSize &maximumImageSize)
{
  showNormal();
  raise();
  activateWindow();
  qApp->processEvents();

  if (ui.mainTabWidget->currentWidget()) {
    ui.mainTabWidget->currentWidget()->adjustSize();
  }
  ui.mainTabWidget->adjustSize();
  adjustSize();
  qApp->processEvents();

  if (maximumWindowSize.isValid()) {
    const QSize natural = sizeHint().expandedTo(minimumSizeHint());
    setFixedSize(natural.boundedTo(maximumWindowSize));
  }
  qApp->processEvents();

  repaint();
  qApp->processEvents();

  QPixmap pixmap;
  if (QScreen *screen = windowHandle() ? windowHandle()->screen() : nullptr) {
    const QRect frame = frameGeometry();
    pixmap = screen->grabWindow(0, frame.x(), frame.y(), frame.width(),
                                frame.height());
    if (pixmap.isNull()) {
      pixmap = screen->grabWindow(winId());
    }
  }
  if (pixmap.isNull()) {
    pixmap = grab();
  }
  if (pixmap.isNull()) {
    qWarning() << "Unable to capture screenshot" << path;
    return false;
  }

  if (maximumImageSize.isValid()) {
    const QSize croppedSize(qMin(pixmap.width(), maximumImageSize.width()),
                            qMin(pixmap.height(), maximumImageSize.height()));
    pixmap = pixmap.copy(QRect(QPoint(0, 0), croppedSize));
  }

  if (!pixmap.save(path, "PNG")) {
    qWarning() << "Unable to save screenshot" << path;
    return false;
  }

  qInfo() << "Saved screenshot" << path;
  return true;
}

/**
 * @brief Captures the disconnected Tag State screen and exits the app.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureStartupScreenshot()
{
  bool ok = true;
  QDir dir(screenshotDir);
  if (!dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  if (ok) {
    ui.mainTabWidget->setCurrentWidget(ui.targetTab);
    waitForGuiToSettle(200);
    ok = saveCurrentScreenshot(screenshotPath("qtmonitor-startup.png"),
                               QSize(700, 760), QSize(760, 790));
  }
  qApp->exit(ok ? 0 : 1);
}

/**
 * @brief Captures representative Tag State screenshots from a fake fixture.
 *
 * @details The current implementation renders idle, running, and finished in
 *          sequence. Missing running or finished samples are derived by
 *          fakeStatusForName() so one idle fixture can still demonstrate state
 *          dependent controls.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureMainScreenshots()
{
  bool ok = true;
  if (!fakeMode) {
    qWarning() << "qtmonitor main screenshots require --fake-fixture";
    ok = false;
  }

  QDir dir(screenshotDir);
  if (ok && !dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  const QString states[] = {"idle", "running", "finished"};
  for (const QString &state : states) {
    if (!ok) {
      break;
    }
    ui.mainTabWidget->setCurrentWidget(ui.targetTab);
    applyFakeState(state);
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(screenshotPath(
        QString("qtmonitor-main-%1.png").arg(state)), QSize(700, 760),
        QSize(760, 790));
  }
  qApp->exit(ok ? 0 : 1);
}

/**
 * @brief Captures idle Configuration-tab screenshots from a fake fixture.
 *
 * @details The fixture id is used in output filenames, and the inner Schedule
 *          and Sensors tabs are selected explicitly before capture.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureConfigScreenshots()
{
  bool ok = true;
  if (!fakeMode) {
    qWarning() << "qtmonitor config screenshots require --fake-fixture";
    ok = false;
  }

  QDir dir(screenshotDir);
  if (ok && !dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  const QString id =
      safeFileId(fakeFixtureId.isEmpty()
                     ? QString::fromStdString(TagType_Name(tag_type))
                     : fakeFixtureId);
  if (ok) {
    applyFakeState("idle");
    ui.mainTabWidget->setCurrentWidget(ui.configtab);
    ui.configtab->ShowScheduleTab();
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(
        screenshotPath(QString("qtmonitor-config-%1-schedule.png").arg(id)),
        QSize(900, 760));
  }
  if (ok && ui.configtab->HasSensorConfiguration()) {
    ui.mainTabWidget->setCurrentWidget(ui.configtab);
    ui.configtab->ShowSensorTab();
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(
        screenshotPath(QString("qtmonitor-config-%1-sensors.png").arg(id)),
        QSize(900, 760));
  }
  qApp->exit(ok ? 0 : 1);
}

/**
 * @brief Captures the Error Log tab and exits the app.
 *
 * @details This capture uses no attached tag, which shows the user-facing log
 *          pane and file-save affordance without requiring hardware or fixture
 *          data.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureErrorLogScreenshot()
{
  bool ok = true;
  QDir dir(screenshotDir);
  if (!dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  if (ok) {
    ui.mainTabWidget->setCurrentWidget(ui.errorTab);
    waitForGuiToSettle(200);
    ok = saveCurrentScreenshot(screenshotPath("qtmonitor-error-log.png"),
                               QSize(700, 560));
  }
  qApp->exit(ok ? 0 : 1);
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
      on_Detach_clicked();
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
 *        Status Tab
 ********************************************/

void MainWindow::on_Attach_clicked()
{
  if (fakeMode) {
    return;
  }
  if (Attach())
  {
    ui.StatusGroup->setEnabled(true);
    ui.TagInformation->setEnabled(true);
    ui.ControlGroup->setEnabled(true);
    ui.Attach->setEnabled(false);
    ui.Detach->setEnabled(true);
  } else {
    setAttachedUiEnabled(false);
  }
}

void MainWindow::on_Detach_clicked()
{
  if (fakeMode) {
    return;
  }
  tag.Detach();
  timer.stop();
  TriggerUpdate();
  setAttachedUiEnabled(false);
  ui.configtab->Detach();
  tag_debug_line_buffer.clear();
}

void MainWindow::on_syncButton_clicked()
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

void MainWindow::on_stopButton_clicked()
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

void MainWindow::on_calibrateButton_clicked()
{
  if (fakeMode) {
    return;
  }
  if (!tagSupportsCalibration(tag_type)) {
    return;
  }
  tag.Calibrate();
}

void MainWindow::on_testButton_clicked()
{
  if (fakeMode) {
    return;
  }
  if (current_state == IDLE)
  {
    std::string msg;
    tag.Test(RUN_ALL); // need to check return !
    ui.testButton->setEnabled(false);
    ui.info_testStatus->setText("Running");
  }
}

void MainWindow::on_eraseButton_clicked()
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

void MainWindow::on_tagLogSaveButton_clicked()
{
  if (fakeMode) {
    return;
  }
  const TagLogStorageFormat storage_format = defaultTagLogStorageFormat(tag_type);
  const std::vector<TagLogStorageFormat> formats = supportedTagLogStorageFormats(tag_type);
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
  ui.datadownloadgroupBox->setEnabled(false);
  connect(download_thread,&QThread::finished,this,[this,restart_status_timer]() {
    ui.datadownloadgroupBox->setEnabled(tag.IsAttached() &&
                                        isDownloadableState(current_state));
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
