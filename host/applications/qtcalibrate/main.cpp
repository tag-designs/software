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
      //level = LOG_INFO;
      s_textEdit->append(msg);
      return;
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

  qInstallMessageHandler(myMessageOutput);
  QApplication a(argc, argv);
  QCommandLineParser parser;
  parser.setApplicationDescription("Tag calibration tool");
  parser.addHelpOption();

  // These options are intentionally hidden in the maintainer workflow rather
  // than the normal UI. They let documentation builds open the real
  // qtcalibrate window, feed it archived calibration data as a fake tag, and
  // capture stable screenshots without requiring hardware on the build host.
  parser.addOption(QCommandLineOption(
      "replay-capture",
      "Load a qtcalibrate sample capture JSON file instead of attaching to USB.",
      "path"));
  parser.addOption(QCommandLineOption(
      "replay-percent",
      "Prefill the replayed calibration collection to a percentage from 0 to 100.",
      "percent"));
  parser.addOption(QCommandLineOption(
      "capture-replay-screenshots",
      "Generate replay milestone screenshots and exit. Requires --replay-capture."));
  parser.addOption(QCommandLineOption(
      "capture-orientation-screenshot",
      "Generate an orientation screenshot from replay data and exit. Requires --replay-capture."));
  parser.addOption(QCommandLineOption(
      "orientation-pose",
      "Pose for orientation screenshots as heading,pitch,roll,dip,field,gravity.",
      "values"));
  parser.addOption(QCommandLineOption(
      "capture-startup-screenshot",
      "Generate the disconnected startup screenshot and exit without attaching."));
  parser.addOption(QCommandLineOption(
      "no-auto-attach",
      "Open the app without probing USB tag bases."));
  parser.addOption(QCommandLineOption(
      "screenshot-dir",
      "Directory for generated screenshots. Defaults to host/docs/src/images.",
      "dir"));
  parser.addOption(QCommandLineOption(
      "screenshot-prefix",
      "Filename prefix for replay milestone screenshots.",
      "prefix",
      "qtcalibrate-collection"));
  parser.process(a);

  MainWindowOptions options;
  options.replayCapturePath = parser.value("replay-capture");
  if (parser.isSet("replay-percent")) {
    bool ok = false;
    const int percent = parser.value("replay-percent").toInt(&ok);
    if (ok) {
      options.replayPercent = qBound(0, percent, 100);
    }
  }
  options.captureReplayScreenshots = parser.isSet("capture-replay-screenshots");
  options.captureOrientationScreenshot =
      parser.isSet("capture-orientation-screenshot");
  options.captureStartupScreenshot = parser.isSet("capture-startup-screenshot");
  options.skipAutoAttach =
      parser.isSet("no-auto-attach") || options.captureStartupScreenshot;
  if (parser.isSet("orientation-pose")) {
    const QStringList values = parser.value("orientation-pose").split(',');
    if (values.size() == 6) {
      for (const QString &value : values) {
        bool ok = false;
        const float number = value.trimmed().toFloat(&ok);
        if (ok) {
          options.orientationPose.append(number);
        } else {
          options.orientationPose.clear();
          break;
        }
      }
    }
  }
  options.screenshotDir = parser.value("screenshot-dir");
  options.screenshotPrefix = parser.value("screenshot-prefix");

  HostStyle::apply(a);
  //vsLogMsg.logMessage = DisplayMessage;
	//vsLogMsg.initProgressBar = InitPBar;
	//vsLogMsg.loadBar = lBar;
 

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  log_set_quiet(true);
  log_set_level(log_level);
  log_add_callback(log_log_callback, nullptr, LOG_TRACE);

  MainWindow w(options);
  w.show();
  if (w.shouldQuitAfterStartup()) {
    // Screenshot slots run after the first event-loop turn so Qt has created
    // native window resources and loaded the embedded QML views.
    if (options.captureStartupScreenshot) {
      QTimer::singleShot(600, &w, &MainWindow::captureStartupScreenshot);
    } else if (options.captureOrientationScreenshot) {
      QTimer::singleShot(900, &w, &MainWindow::captureOrientationScreenshot);
    } else {
      QTimer::singleShot(600, &w, &MainWindow::captureReplayScreenshots);
    }
  }
  return a.exec();
}
