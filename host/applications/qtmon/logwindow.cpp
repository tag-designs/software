/**
 * @file    logwindow.cpp
 * @brief   Error Log tab implementation for qtmonitor.
 *
 * @details The tab redirects Qt/log.c messages into a read-only text edit and
 *          exposes save, clear, and log-level controls. The explicit size hints
 *          keep the documentation Error Log screenshot compact without
 *          clipping the bottom of the text edit.
 */

#include "logwindow.h"
#include <QTextEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QSize>
#include <QStringList>

#include "txtlogs.h"
#include "qtfiledialog.h"


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

/**
 * @brief Returns the preferred Error Log tab size.
 *
 * @return Documentation-friendly preferred size in pixels.
 */
QSize LogWindow::sizeHint() const
{
    return QSize(700, 460);
}

/**
 * @brief Returns the smallest useful Error Log tab size.
 *
 * @return Minimum size that preserves the toolbar row and log pane.
 */
QSize LogWindow::minimumSizeHint() const
{
    return QSize(520, 320);
}

bool LogWindow::Attach(Tag &tag){return true;}

// these help with debugging UI
// we should disable them except when debug level is enabled

/*
void LogWindow::on_LogConfigButton_clicked()
{
  Config tmp;
  configtab_->GetConfig(tmp);
  logtab->append(QString::fromStdString(tmp.DebugString()));
}

void LogWindow::on_LogTagConfigButton_clicked()
{
  Config tmp;
  if (tag.GetConfig(tmp))
  {
    logtab->append(QString::fromStdString(tmp.DebugString()));
  }
  else
  {
    qDebug() << "tag.GetConfig() returned false";
  }
}
*/

void LogWindow::on_loglevelBox_currentIndexChanged(int index)
{
  log_set_level(index);
  log_level = index;
}

// Save the log window contents as a text file

void LogWindow::on_logsaveButton_clicked()
{
  QString nameFile = HostFileDialog::getSaveFileName(
      this, tr("Save Log"), QDir::homePath());
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

void LogWindow::on_logclearButton_clicked()
{ 
  qDebug() << "Clearing log";
  ui.logTextEdit->clear();
}
