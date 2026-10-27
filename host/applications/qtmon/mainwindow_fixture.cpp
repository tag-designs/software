/**
 * @file    mainwindow_fixture.cpp
 * @brief   MainWindow's maintainer-only support: documentation screenshot
 *          capture and the action wiring check.
 *
 * @details None of this runs in normal use. With `--fake-fixture` the window's
 *          session is a FixtureSession (session/fixturesession.h), which
 *          replays a captured tag through the same window; the `--capture-*`
 *          options save its screens for the user manual and exit. The
 *          Configuration tab's headless test mode is separate, in
 *          configprobe.cpp. See README.md in this directory.
 */

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QWindow>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "fixturesession.h"
#include "tagconfiguration/configtab.h"
#include "tagstatusstrip.h"

namespace
{

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
    return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath("host/docs/src/images");
  }

  const QFileInfo requested(path);
  if (requested.isAbsolute()) {
    return requested.filePath();
  }

  return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path);
}

bool MainWindow::shouldQuitAfterStartup() const
{
  return captureStartupScreenshotOnStartup || captureMainScreenshotsOnStartup
      || captureConfigScreenshotsOnStartup || captureErrorLogScreenshotOnStartup;
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
 * @brief Captures Tag State screenshots for idle, running and finished from
 *        the fixture.
 *
 * @details FixtureSession::showState() reports each state as a status, so the
 *          window draws it exactly as it would a live tag's. Missing running
 *          or finished samples are derived there from the idle one.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureMainScreenshots()
{
  bool ok = true;
  if (!fixture_) {
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
    fixture_->showState(state);
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(screenshotPath(QString("qtmonitor-main-%1.png").arg(state)),
                               QSize(1100, 760), QSize(1160, 790));
  }
  qApp->exit(ok ? 0 : 1);
}

/**
 * @brief Captures the fixture's idle Configuration tab.
 *
 * @details The fixture id is used in the output filename.
 *
 * @post The process exits with status 0 on success and 1 on failure.
 */
void MainWindow::captureConfigScreenshots()
{
  bool ok = true;
  if (!fixture_) {
    qWarning() << "qtmonitor config screenshots require --fake-fixture";
    ok = false;
  }

  QDir dir(screenshotDir);
  if (ok && !dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  if (ok) {
    const QString id = safeFileId(fixture_->id());
    fixture_->showState("idle");
    ui.mainTabWidget->setCurrentWidget(ui.configtab);
    waitForGuiToSettle(250);
    ok = saveCurrentScreenshot(screenshotPath(QString("qtmonitor-config-%1.png").arg(id)),
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
    ok = saveCurrentScreenshot(screenshotPath("qtmonitor-error-log.png"), QSize(1100, 560));
  }
  qApp->exit(ok ? 0 : 1);
}

QStringList MainWindow::checkActionWiring()
{
  // Each command's action, found by the text the menus show rather than
  // through actionFor(), so a wrong entry there cannot hide itself.
  const QList<QPair<qtmon::Command, QString>> texts = {
      {qtmon::Command::Attach, tr("Attach")},
      {qtmon::Command::Detach, tr("Detach")},
      {qtmon::Command::Read, tr("Read Configuration")},
      {qtmon::Command::Start, tr("Start")},
      {qtmon::Command::Stop, tr("Stop")},
      {qtmon::Command::Sync, tr("Synchronize Clock")},
      {qtmon::Command::Test, tr("Self-Test")},
      {qtmon::Command::Erase, tr("Erase...")},
      {qtmon::Command::Download, tr("Download Data...")},
      {qtmon::Command::Open, tr("Open Configuration...")},
      {qtmon::Command::Save, tr("Save Configuration")},
      {qtmon::Command::SaveAs, tr("Save Configuration As...")},
      {qtmon::Command::Reset, tr("Reset to Defaults")}};
  QStringList problems;
  QMap<qtmon::Command, const QAction *> byText;
  for (const auto &[command, text] : texts) {
    const QAction *found = nullptr;
    int matches = 0;
    for (const QAction *action : findChildren<QAction *>()) {
      if (action->text() == text) {
        found = action;
        ++matches;
      }
    }
    if (matches != 1)
      problems << QString("%1 actions are labelled \"%2\"; expected one").arg(matches).arg(text);
    byText.insert(command, found);
  }
  if (!problems.isEmpty())
    return problems;

  for (const qtmon::CommandInputs &in : qtmon::actionStateCases()) {
    applyCommandInputs(in);
    for (qtmon::Command command : qtmon::kAllCommands) {
      const QAction *action = byText.value(command);
      const qtmon::Availability expected = qtmon::availability(command, in);
      const qtmon::Availability shown = !action->isVisible()  ? qtmon::Availability::Hidden
                                        : action->isEnabled() ? qtmon::Availability::Enabled
                                                              : qtmon::Availability::Disabled;
      if (shown != expected)
        problems << QString("%1 %2 %3: %4 is %5, the rule says %6")
                        .arg(in.attached)
                        .arg(QString::fromStdString(std::string(TagState_Name(in.state))),
                             QString::fromStdString(std::string(TagType_Name(in.tag_type))),
                             QString::fromLatin1(qtmon::commandName(command)),
                             QString::fromLatin1(qtmon::availabilityLetter(shown)),
                             QString::fromLatin1(qtmon::availabilityLetter(expected)));
    }
  }

  // Leave the actions as the model says.
  updateActions();
  return problems;
}

namespace
{

/**
 * @brief Appends @p actions as menu entries, a submenu's entries indented
 *        under it.
 * @param[in,out] lines    Output lines.
 * @param[in]     actions  The entries, in order.
 * @param[in]     indent   Leading spaces for this level.
 */
void appendMenuEntries(QStringList &lines, const QList<QAction *> &actions, int indent)
{
  const QString pad(indent, QLatin1Char(' '));
  for (const QAction *action : actions) {
    if (action->isSeparator()) {
      lines << pad + "---";
      continue;
    }
    QString line = pad + action->text();
    if (!action->shortcut().isEmpty())
      line += "  [" + action->shortcut().toString(QKeySequence::PortableText) + "]";
    if (!action->toolTip().isEmpty() && action->toolTip() != action->text())
      line += "  -- " + action->toolTip();
    if (action->isCheckable())
      line += action->isChecked() ? "  (checked)" : "  (unchecked)";
    lines << line;
    if (action->menu())
      appendMenuEntries(lines, action->menu()->actions(), indent + 2);
  }
}

} // namespace

QString MainWindow::menuTable()
{
  QStringList lines;
  for (const QAction *top : menuBar()->actions()) {
    lines << "menu " + top->text();
    if (top->menu())
      appendMenuEntries(lines, top->menu()->actions(), 2);
  }
  lines << "popup Tag page";
  appendMenuEntries(lines, ui.targetTab->actions(), 2);
  lines << "popup status strip";
  appendMenuEntries(lines, status_strip_->actions(), 2);
  lines << "popup Configuration tab";
  appendMenuEntries(lines, ui.configtab->actions(), 2);
  return lines.join('\n') + '\n';
}
