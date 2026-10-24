/**
 * @file    main.cpp
 * @brief   Tag Programmer's entry point, log routing and command-line modes.
 *
 * @details Routes tagcore's log callback to the log pane and recognises
 *          `--flash-plan`, `--verify` and `--capture-screenshots` (see main()).
 */

#include <QApplication>
#include <QString>

#include <QMainWindow>
#include <QTextEdit>
#include <QTimer>
#include <string>
#include "tag.pb.h"
#include "tagclass.h"

#include "firmwareimage.h"
#include "flashplan.h"
#include "mainwindow.h"
#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include "qthoststyle.h"

extern "C"
{
#include "log.h"
}



//  Redirect log messages to text window
//  based upon:
//  http://stackoverflow.com/questions/22485208/redirect-qdebug-to-qtextedit

/// The log pane's text box, owned by LogWindow; null until it is built.
extern QTextEdit *s_textEdit;
/// Lowest level shown in the log pane: tagcore's LOG_* scale. Not changed after start-up.
int log_level = LOG_ERROR;

/**
 * @brief Appends one message to the log pane, when it is at or above
 *        log_level and the pane exists.
 * @param[in] ll       tagcore log level (LOG_TRACE..LOG_FATAL).
 * @param[in] context  Source location; shown when it has a line number.
 * @param[in] msg      The message text.
 */
static void logOutput(int ll, const QMessageLogContext &context,
               const QString &msg)
{
  const char *tag;

  if ((ll < log_level) || (s_textEdit == nullptr))
    return;
  tag = log_level_string(ll); 
  if (context.line)
    s_textEdit->append(QString("%1 %2 %3:%4")
                           .arg(tag)
                           .arg(context.function)
                           .arg(context.line)
                           .arg(msg));
  else
    s_textEdit->append(QString("%1 %4").arg(tag).arg(msg));
}

/**
 * @brief tagcore log callback: formats the event and passes it to logOutput().
 * @param[in] ev  The event; its format string and arguments are consumed.
 */
void log_log_callback(log_Event *ev)
{
  QMessageLogContext context;
  context.file = ev->file;
  context.line = ev->line;
  context.function = "";

  if (ev->level >= log_level)
  {
    QString msg = QString::vasprintf(ev->fmt, ev->ap);
    logOutput(ev->level, context, msg);
  }
}

/**
 * @brief Qt message handler mapping Qt levels to tagcore levels for logOutput().
 * @details Installed by LogWindow's constructor (qInstallMessageHandler), so
 *          qDebug()/qInfo()/qWarning() reach the log pane.
 * @warning Aborts on QtFatalMsg.
 */
void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                     const QString &msg)
{
  int level;
  if (s_textEdit != nullptr)
  {
    switch (type)
    {
    case QtInfoMsg:
      level = LOG_INFO;
      break;
    case QtDebugMsg:
      level = LOG_DEBUG;
      break;
    case QtWarningMsg:
      level = LOG_WARN;
      break;
    case QtCriticalMsg:
      level = LOG_ERROR;
      break;
    case QtFatalMsg:
      abort();
    }
    logOutput(level, context, msg);
  }
}

/**
 * @brief Starts Tag Programmer, or runs one of its command-line modes.
 *
 * @details `--flash-plan <elf>...` prints each image's planned erase and
 *          exits, with no window, tag or programmer.
 *          `--verify <elf|release dir>...` loads each image as Program would:
 *          its identity record, its erase, and its hash against the build
 *          manifest beside it (as `flash_release.py --verify-only` does), and
 *          prints the result; a directory is a release's `firmware/<Target>`
 *          or the `firmware` directory holding several.
 *          `--capture-screenshots [<dir>]` opens the window, captures the user
 *          guide's screenshots (MainWindow::captureScreenshots()) and exits.
 *          `--bench-program <label>` runs the checks, programs the tag on the
 *          base as Program does and exits with the result
 *          (MainWindow::benchProgram()); it erases the tag.
 *          Otherwise the window runs until closed. Settings are stored under
 *          organization `tag-designs`, application `qtprogram`.
 *
 * @return The application's exit code; from `--verify`, 1 if any image is
 *         refused.
 */
int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("tag-designs"));
  QCoreApplication::setApplicationName(QStringLiteral("qtprogram"));

  // qtprogram --flash-plan <elf>...: print the erase each image would get, and
  // exit. Checks the plan without a tag or programmer.
  const QStringList args = QCoreApplication::arguments();
  if (args.size() > 1 && args.at(1) == QLatin1String("--flash-plan")) {
    for (const QString &path : args.mid(2)) {
      const FlashPlan plan = planFlash(path);
      std::printf("%s | %s | %s\n", qPrintable(QFileInfo(path).fileName()),
                  qPrintable(plan.eraseArgs().join(' ')), qPrintable(plan.summary()));
    }
    return 0;
  }
  if (args.size() > 1 && args.at(1) == QLatin1String("--verify")) {
    int status = 0;
    QStringList images;
    for (const QString &path : args.mid(2)) {
      const QFileInfo info(path);
      if (!info.isDir()) {
        images << path;
        continue;
      }
      const QString own = QDir(path).filePath(info.fileName() + ".elf");
      if (QFileInfo::exists(own)) {
        images << own;
        continue;
      }
      // Directories without their own ELF (firmware/licenses) are not images.
      for (const QString &target : QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString elf = QDir(QDir(path).filePath(target)).filePath(target + ".elf");
        if (QFileInfo::exists(elf))
          images << elf;
      }
    }
    for (const QString &path : images) {
      const FirmwareImage image = loadFirmwareImage(path);
      const QString target = image.identity.found ? image.identity.target
                                                  : QStringLiteral("no identity record");
      const QString manifest = image.manifestVerified        ? QStringLiteral("verified")
                               : image.manifestPath.isEmpty() ? QStringLiteral("none")
                                                              : QStringLiteral("REFUSED");
      std::printf("%s\n  target    %s (%s, TagType %d)\n  commit    %s\n  sha256    %s\n"
                  "  manifest  %s\n  erase     %s\n",
                  qPrintable(path), qPrintable(target), qPrintable(image.identity.boardDesc),
                  image.identity.tagType, qPrintable(image.identity.gitHash),
                  qPrintable(image.sha256), qPrintable(manifest),
                  qPrintable(image.plan.summary()));
      if (!image.warning.isEmpty())
        std::printf("  warning   %s\n", qPrintable(image.warning));
      if (!image.usable()) {
        std::printf("  REFUSED   %s\n", qPrintable(image.error));
        status = 1;
      }
    }
    return status;
  }
  HostStyle::apply(a);
  //vsLogMsg.logMessage = DisplayMessage;
	//vsLogMsg.initProgressBar = InitPBar;
	//vsLogMsg.loadBar = lBar;
 

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  log_set_quiet(true);
  log_set_level(log_level);
  log_add_callback(log_log_callback, nullptr, LOG_TRACE);

  MainWindow w;
  w.show();
  QTimer::singleShot(0, &w, &MainWindow::start);

  // qtprogram --capture-screenshots [<dir>]: the user guide's screenshots
  // (default dir host/docs/src/images). Attaches to the tag when attaching
  // automatically is allowed, but never erases or programs it.
  // qtprogram --bench-program <label>: a bench smoke test of the whole
  // station. Erases and programs the tag on the base, then exits.
  if (args.size() > 2 && args.at(1) == QLatin1String("--bench-program"))
    QTimer::singleShot(100, &w, [&w, args]() { w.benchProgram(args.at(2)); });
  if (args.size() > 1 && args.at(1) == QLatin1String("--capture-screenshots")) {
    const QString dir = args.size() > 2
        ? args.at(2)
        : QStringLiteral(TAG_DESIGNS_SOURCE_DIR "/host/docs/src/images");
    QTimer::singleShot(100, &w, [&w, dir]() { w.captureScreenshots(dir); });
  }
  return a.exec();
}
