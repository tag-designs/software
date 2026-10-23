/**
 * @file    main.cpp
 * @brief   qtmonitor entry point: command line, logging hooks, and the
 *          headless modes that run without a window.
 *
 * @details Normal startup opens MainWindow, which probes USB and attaches to a
 *          tag. Two options exit before any window opens:
 *          `--check-config-panels` (ConfigTab::CheckPanelsAgainstShapes()) and
 *          `--config-probe` (runConfigProbe()). The `--fake-fixture` and
 *          `--capture-*` options drive documentation screenshots; see
 *          README.md in this directory.
 */

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>

#include <QMainWindow>
#include <QTextEdit>
#include <QTimer>
#include <string>
#include "tag.pb.h"
#include "tagclass.h"

/* #include "tag.pb.h"
#include "host.pb.h"
#include "tagclass.h" */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qthoststyle.h"
#include "configprobe.h"
#include "tagconfiguration/configtab.h"
#include <cstdio>

#include "mainwindow.h"

extern "C"
{
#include "log.h"
}



//  Redirect log messages to text window
//  based upon:
//  http://stackoverflow.com/questions/22485208/redirect-qdebug-to-qtextedit

/// The Error Log tab's text box, defined in logwindow.cpp; null until created.
extern QTextEdit *s_textEdit;
/// Lowest level (LOG_TRACE..LOG_FATAL) shown in the Error Log tab.
int log_level = LOG_ERROR;

/**
 * @brief Appends one message to the Error Log tab's text box.
 *
 * @param[in] ll       Level, LOG_TRACE..LOG_FATAL; below log_level is dropped.
 * @param[in] context  Source location; the function and line are shown when
 *                     the line is set.
 * @param[in] msg      Message text.
 *
 * @note Does nothing until the Error Log tab has created s_textEdit.
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
 * @brief tagcore log callback: forwards tagcore's C log messages at or above
 *        log_level to the Error Log tab.
 * @param[in] ev  The log event, with printf-style format and arguments.
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
 * @brief Qt message handler, installed by the Error Log tab: forwards
 *        qDebug(), qInfo(), qWarning() and qCritical() to it.
 *
 * @param[in] type     Qt message type, mapped to LOG_DEBUG..LOG_ERROR.
 * @param[in] context  Source location.
 * @param[in] msg      Message text.
 *
 * @warning A QtFatalMsg aborts the process, as Qt's default handler does.
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
 * @brief Parses the command line, then runs a headless mode or opens the
 *        main window.
 * @return The headless mode's status, or the Qt event loop's exit code.
 */
int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  // QSettings (recent configuration files) is stored under these names.
  QCoreApplication::setOrganizationName(QStringLiteral("tag-designs"));
  QCoreApplication::setApplicationName(QStringLiteral("qtmonitor"));
  QCommandLineParser parser;
  parser.setApplicationDescription("Tag monitor");
  parser.addHelpOption();
  parser.addOption(QCommandLineOption(
      "fake-fixture",
      "Load a qtmonitor fake-tag fixture JSON file instead of attaching to USB.",
      "path"));
  parser.addOption(QCommandLineOption(
      "fake-state",
      "Fixture state slot for single-state display. Defaults to idle.",
      "name",
      "idle"));
  parser.addOption(QCommandLineOption(
      "capture-startup-screenshot",
      "Capture the disconnected startup screen and exit."));
  parser.addOption(QCommandLineOption(
      "capture-main-screenshots",
      "Capture idle, running, and finished Tag State screenshots and exit."));
  parser.addOption(QCommandLineOption(
      "capture-config-screenshots",
      "Capture the idle Configuration tab screenshot and exit."));
  parser.addOption(QCommandLineOption(
      "capture-error-log-screenshot",
      "Capture the disconnected Error Log screen and exit."));
  parser.addOption(QCommandLineOption(
      "screenshot-dir",
      "Directory for generated screenshots. Defaults to host/docs/src/images.",
      "dir"));
  parser.addOption(QCommandLineOption(
      "no-auto-attach",
      "Open without probing USB tag bases."));
  parser.addOption(QCommandLineOption(
      "config-probe",
      "Run configuration-screen test cases (a case file or a directory of "
      "them) with no tag attached, write results and exit. See "
      "host/applications/qtmon/test/README.md.",
      "cases"));
  parser.addOption(QCommandLineOption(
      "config-probe-out",
      "Directory for --config-probe results. Defaults to config-probe-results.",
      "dir",
      "config-probe-results"));
  parser.addOption(QCommandLineOption(
      "check-action-states",
      "Print which actions are enabled for every combination of tag state and "
      "the other inputs, and exit."));
  parser.addOption(QCommandLineOption(
      "check-start-checks",
      "Print which start checks fail for every combination of clock offset, "
      "self-test result and approval, and exit."));
  parser.addOption(QCommandLineOption(
      "check-config-panels",
      "Check which configuration panels each tag type shows against the tags' "
      "default configurations, print any mismatch and exit (1 on mismatch)."));
  parser.process(a);

  if (parser.isSet("check-config-panels")) {
    HostStyle::apply(a);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    ConfigTab tab;
    const QStringList problems = tab.CheckPanelsAgainstShapes();
    for (const QString &problem : problems)
      std::fprintf(stderr, "%s\n", qPrintable(problem));
    std::printf("config panels: %s\n",
                problems.isEmpty() ? "agree with every tag's default configuration"
                                   : "MISMATCH");
    return problems.isEmpty() ? 0 : 1;
  }

  if (parser.isSet("config-probe")) {
    HostStyle::apply(a);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    return runConfigProbe(parser.value("config-probe"),
                          parser.value("config-probe-out"));
  }

  if (parser.isSet("check-start-checks")) {
    HostStyle::apply(a);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    MainWindowOptions options;
    options.skipAutoAttach = true;
    MainWindow window(options);
    std::fputs(qPrintable(window.startCheckTable()), stdout);
    return 0;
  }

  if (parser.isSet("check-action-states")) {
    HostStyle::apply(a);
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    MainWindowOptions options;
    options.skipAutoAttach = true;
    MainWindow window(options);
    std::fputs(qPrintable(window.actionStateTable()), stdout);
    return 0;
  }

  MainWindowOptions windowOptions;
  windowOptions.fakeFixturePath = parser.value("fake-fixture");
  windowOptions.fakeState = parser.value("fake-state");
  windowOptions.screenshotDir = parser.value("screenshot-dir");
  windowOptions.captureStartupScreenshot =
      parser.isSet("capture-startup-screenshot");
  windowOptions.captureMainScreenshots =
      parser.isSet("capture-main-screenshots");
  windowOptions.captureConfigScreenshots =
      parser.isSet("capture-config-screenshots");
  windowOptions.captureErrorLogScreenshot =
      parser.isSet("capture-error-log-screenshot");
  windowOptions.skipAutoAttach =
      parser.isSet("no-auto-attach") || windowOptions.captureStartupScreenshot
      || windowOptions.captureErrorLogScreenshot;

  HostStyle::apply(a);

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  log_set_quiet(true);
  log_set_level(log_level);
  log_add_callback(log_log_callback, nullptr, LOG_TRACE);
  MainWindow w(windowOptions);
  w.show();
  if (w.shouldQuitAfterStartup()) {
    if (windowOptions.captureStartupScreenshot) {
      QTimer::singleShot(600, &w, &MainWindow::captureStartupScreenshot);
    } else if (windowOptions.captureMainScreenshots) {
      QTimer::singleShot(900, &w, &MainWindow::captureMainScreenshots);
    } else if (windowOptions.captureErrorLogScreenshot) {
      QTimer::singleShot(600, &w, &MainWindow::captureErrorLogScreenshot);
    } else {
      QTimer::singleShot(900, &w, &MainWindow::captureConfigScreenshots);
    }
  }
  return a.exec();
}
