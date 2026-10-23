/**
 * @file    logwindow.cpp
 * @brief   Error Log tab implementation for qtmonitor.
 *
 * @details The tab redirects Qt/log.c messages into a read-only text edit.
 *          Save, clear and the log level are actions, placed in MainWindow's
 *          menus and on the text edit's popup menu. The explicit size hints
 *          keep the documentation Error Log screenshot compact without
 *          clipping the bottom of the text edit.
 */

#include "logwindow.h"
#include <QTextEdit>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
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

    save_ = new QAction(tr("Save Error Log..."), this);
    save_->setToolTip(tr("Save the error log to a text file"));
    connect(save_, &QAction::triggered, this, &LogWindow::saveLog);

    clear_ = new QAction(tr("Clear Error Log"), this);
    clear_->setToolTip(tr("Clear the error log"));
    connect(clear_, &QAction::triggered, this, &LogWindow::clearLog);

    // One checkable action per level; LOG_FATAL is not offered.
    level_menu_ = new QMenu(tr("Log Level"), this);
    levels_ = new QActionGroup(this);
    levels_->setExclusive(true);
    for (int i = 0; i < LOG_FATAL; i++) {
        QAction *level = level_menu_->addAction(log_level_string(i));
        level->setCheckable(true);
        level->setData(i);
        levels_->addAction(level);
    }
    connect(levels_, &QActionGroup::triggered, this, &LogWindow::setLogLevel);
    levels_->actions().at(LOG_INFO)->setChecked(true);
    setLogLevel(levels_->actions().at(LOG_INFO));

    ui.logTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.logTextEdit, &QWidget::customContextMenuRequested, this,
            &LogWindow::showContextMenu);

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

void LogWindow::setLogLevel(QAction *level)
{
  const int index = level->data().toInt();
  log_set_level(index);
  log_level = index;
}

void LogWindow::showContextMenu(const QPoint &pos)
{
  QMenu *menu = ui.logTextEdit->createStandardContextMenu(pos);
  menu->addSeparator();
  menu->addAction(save_);
  menu->addAction(clear_);
  menu->addMenu(level_menu_);
  menu->exec(ui.logTextEdit->viewport()->mapToGlobal(pos));
  delete menu;
}

// Save the log window contents as a text file

void LogWindow::saveLog()
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

void LogWindow::clearLog()
{ 
  qDebug() << "Clearing log";
  ui.logTextEdit->clear();
}
