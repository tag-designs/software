    
#include "logwindow.h"
#include <QTextEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QStringList>

#include "taglogs.h"


extern "C"
{
#include "log.h"
}


// hook into the error logging system

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);
extern int log_level;
QTextEdit *s_textEdit = nullptr;

LogWindow::LogWindow(QWidget *parent) : QWidget(parent) {
    ui.setupUi(this);
    
    // create loglevel choices

    QStringList ll;

    // don't include LOG_FATAL in choices

    for (int i = 0; i < LOG_FATAL; i++) {
        ll << log_level_string(i);
    }

    ui.loglevelBox->addItems(ll);
    ui.loglevelBox->setCurrentIndex(LOG_INFO);

    // connect log text edit box to error logging system

    s_textEdit = ui.logTextEdit;
    qInstallMessageHandler(myMessageOutput);

    //ui.mainTabWidget->setCurrentIndex(0);
}

LogWindow::~LogWindow(){
    s_textEdit = nullptr;
}


void LogWindow::on_loglevelBox_currentIndexChanged(int index)
{
  log_set_level(index);
  log_level = index;
}

// Save the log window contents as a text file

void LogWindow::on_logsaveButton_clicked()
{
  QFileDialog fd;

  fd.setDirectory(QDir::homePath());
  QString nameFile = fd.getSaveFileName();
  if (nameFile != "")
  {
    QFile file(nameFile);

    if (file.open(QIODevice::ReadWrite))
    {
      QTextStream stream(&file);
      stream << ui.logTextEdit->toPlainText();
      file.flush();
      file.close();
    }
    else
    {
      QMessageBox::critical(this, tr("Error"), tr("Cannot open file"));
      return;
    }
  }
}