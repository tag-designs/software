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

#include "mainwindow.h"

extern "C"
{
#include "log.h"
}



//  Redirect log messages to text window
//  based upon:
//  http://stackoverflow.com/questions/22485208/redirect-qdebug-to-qtextedit

extern QTextEdit *s_textEdit;
int log_level = LOG_ERROR;

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

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
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
      "Capture idle Configuration schedule and sensor screenshots and exit."));
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
  parser.process(a);

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
