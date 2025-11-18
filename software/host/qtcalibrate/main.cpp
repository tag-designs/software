#include <QApplication>
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
  //vsLogMsg.logMessage = DisplayMessage;
	//vsLogMsg.initProgressBar = InitPBar;
	//vsLogMsg.loadBar = lBar;
 

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  log_set_quiet(true);
  log_set_level(log_level);
  log_add_callback(log_log_callback, nullptr, LOG_TRACE);

  MainWindow w;
  w.show();
  return a.exec();
}
