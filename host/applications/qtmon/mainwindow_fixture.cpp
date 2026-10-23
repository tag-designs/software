/**
 * @file    mainwindow_fixture.cpp
 * @brief   MainWindow's maintainer-only test support: fixture replay and
 *          documentation screenshot capture.
 *
 * @details None of this runs when qtmonitor attaches a real tag. Fixture mode
 *          (`--fake-fixture`) loads a captured tag from protobuf JSON and
 *          drives the same widgets live mode uses, so screenshots show the
 *          real UI; the `--capture-*` options save those screens for the user
 *          manual and exit. The live window is in mainwindow.cpp. The
 *          Configuration tab's headless test mode is separate, in
 *          configprobe.cpp. See README.md in this directory.
 */

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QWindow>

#include <google/protobuf/util/json_util.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "tag.pb.h"

#include "tagconfiguration/configtab.h"
#include "tagstatusstrip.h"

namespace
{

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
QString MainWindow::resolveScreenshotDir(const QString &path)
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
    QSize natural = sizeHint().expandedTo(minimumSizeHint());
    // QTabWidget is as tall as its tallest page, so a short page would be
    // captured in a window sized for another tab. Take the difference between
    // the tallest page and the page on screen back off the window's height.
    if (const QWidget *current = ui.mainTabWidget->currentWidget()) {
      int tallest = 0;
      for (int i = 0; i < ui.mainTabWidget->count(); ++i) {
        const QWidget *page = ui.mainTabWidget->widget(i);
        tallest = qMax(tallest,
                       page->sizeHint().expandedTo(page->minimumSizeHint()).height());
      }
      const int own =
          current->sizeHint().expandedTo(current->minimumSizeHint()).height();
      natural.setHeight(natural.height() - (tallest - own));
    }
    setFixedSize(natural.boundedTo(maximumWindowSize));
    // The window system applies a resize asynchronously; grabbing the frame
    // rectangle before it has would capture whatever lies behind the window.
    waitForGuiToSettle(250);
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
                               QSize(1100, 760), QSize(1160, 790));
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
        QString("qtmonitor-main-%1.png").arg(state)), QSize(1100, 760),
        QSize(1160, 790));
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
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(
        screenshotPath(QString("qtmonitor-config-%1.png").arg(id)),
        QSize(1100, 1400));
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
                               QSize(1100, 560));
  }
  qApp->exit(ok ? 0 : 1);
}

QString MainWindow::actionStateTable()
{
  const QList<QPair<QString, QAction *>> actions = {
      {"attach", attach_action_},   {"detach", detach_action_},
      {"read", read_config_action_}, {"start", start_action_},
      {"stop", stop_action_},       {"sync", sync_action_},
      {"test", test_action_},       {"calibrate", calibrate_action_},
      {"erase", erase_action_},     {"download", download_action_},
      {"open", open_config_action_}, {"save", save_config_action_},
      {"saveas", save_as_config_action_}, {"reset", reset_config_action_}};

  QStringList lines;
  QString header = "attached state tagtype document downloading testpending |";
  for (const auto &a : actions)
    header += " " + a.first;
  lines << header;

  // document: "none", "same" (the tag's type) or "other" (another type).
  auto row = [&](bool attached, TagState state, TagType type, const QString &document,
                 bool downloading, bool pending) {
    attached_ui_ = attached;
    current_state = state;
    tag_type = type;
    downloading_ = downloading;
    test_pending_ = pending;
    if (document == "none") {
      ui.configtab->Clear();
    } else {
      Config c;
      c.set_tag_type(document == "same" ? type : (type == IMUTAG ? BITTAG_LE : IMUTAG));
      ui.configtab->SetConfig(c);
    }
    updateActions();
    QString line = QString("%1 %2 %3 %4 %5 %6 |")
                       .arg(attached).arg(QString::fromStdString(std::string(TagState_Name(state))))
                       .arg(QString::fromStdString(std::string(TagType_Name(type))))
                       .arg(document).arg(downloading).arg(pending);
    for (const auto &a : actions)
      line += QString(" %1=%2").arg(a.first,
                                    !a.second->isVisible() ? "H"
                                    : a.second->isEnabled() ? "E" : "D");
    lines << line;
  };

  const auto *states = TagState_descriptor();
  for (TagType type : {BITTAG_LE, COMPASSTAG}) {
    for (const char *document : {"none", "same"})
      row(false, IDLE, type, document, false, false);
    for (int i = 0; i < states->value_count(); ++i) {
      const auto state = static_cast<TagState>(states->value(i)->number());
      row(true, state, type, "same", false, false);
    }
    row(true, IDLE, type, "none", false, false);    // attached, no document
    row(true, IDLE, type, "other", false, false);   // document for another type
    row(true, IDLE, type, "same", false, true);     // self-test pending
    row(true, FINISHED, type, "same", true, false); // download running
  }

  // Leave the window as a detached qtmonitor.
  attached_ui_ = false;
  downloading_ = false;
  test_pending_ = false;
  ui.configtab->Clear();
  updateActions();
  return lines.join('\n') + '\n';
}

QString MainWindow::startCheckTable()
{
  QStringList lines;
  lines << "clock_offset_s self_test approved | failing checks";
  const double offsets[] = {0.0, 1.99, -2.0, 2.01, -30.0, -1.45e9};
  const TestResult tests[] = {ALL_PASSED, TEST_UNSPECIFIED, TEST_RUNNING, RTC_FAILED};
  for (double offset : offsets) {
    for (TestResult test : tests) {
      for (bool approved : {true, false}) {
        clock_offset_s_ = offset;
        last_test_ = test;
        status_strip_->setApprovalAvailable(true);
        status_strip_->setApproved(approved);
        QStringList failing;
        const QStringList failures = startCheckFailures();
        for (const QString &f : failures)
          failing << (f.startsWith("Clock") ? "clock"
                      : f.startsWith("Self-test") ? "self-test" : "approval");
        lines << QString("%1 %2 %3 | %4")
                     .arg(offset, 0, 'g', 4)
                     .arg(QString::fromStdString(std::string(TestResult_Name(test))))
                     .arg(approved)
                     .arg(failing.isEmpty() ? QStringLiteral("none") : failing.join(' '));
      }
    }
  }
  clock_offset_s_ = 0.0;
  last_test_ = TEST_UNSPECIFIED;
  status_strip_->setApproved(false);
  return lines.join('\n') + '\n';
}
