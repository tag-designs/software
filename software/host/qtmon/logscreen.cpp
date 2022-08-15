#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLayout>
#include <QLabel>
#include <QFile>
#include <QFileDialog>
#include <QDebug>
#include <logscreen.h>
#include <QMessageBox>
#include <QTextStream>

extern "C"
{
#include "log.h"
}

extern int log_level;
QTextEdit *s_textEdit = nullptr;

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);

LogScreen::LogScreen(QWidget *parent)
{
    QStringList ll;

    // don't include LOG_FATAL in choices

    for (int i = 0; i < LOG_FATAL; i++) {
      ll << log_level_string(i);
    }
    
    // create layouts 

    vbox = new QVBoxLayout();
    QHBoxLayout *hbox = new QHBoxLayout();
    QHBoxLayout *dhbox = new QHBoxLayout();

    // create top controls

    QWidget *top = new QWidget();

    saveButton = new QPushButton("Save");
    saveButton->setToolTip("Save error log to file");

    loglevelBox = new QComboBox();
    loglevelBox->addItems(ll);
    //loglevelBox->setCurrentIndex(debug_level - LOG_LVL_SILENT);
    loglevelBox->setCurrentIndex(log_level);
    loglevelBox->setToolTip("Set error logging level");
    QLabel *label = new QLabel("Log Level");
    hbox->addWidget(saveButton);
    hbox->insertStretch(1);
    hbox->addWidget(label);
    hbox->addWidget(loglevelBox);
    top->setLayout(hbox);

    // create text window

    textEdit = new QTextEdit();
    textEdit->setReadOnly(true);
    textEdit->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QWidget *bottom = new QWidget();
    
    printButton = new QPushButton("Print Config");
    printTagButton = new QPushButton("Print Tag Config");


    printButton->setToolTip("Print configuration as set in UI");
    printTagButton->setToolTip("Print configuration from tag");
    
    dhbox->addWidget(printButton);
    dhbox->addWidget(printTagButton);
    dhbox->insertStretch(0);
    bottom->setLayout(dhbox);

    // assemble everything

    vbox->addWidget(top);
    vbox->addWidget(textEdit);
    vbox->addWidget(bottom);
    setLayout(vbox);
   
    // connect signals/slots

    connect(saveButton, &QPushButton::clicked, this,
            &LogScreen::on_logsaveButton_clicked);

    connect(loglevelBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
       &LogScreen::on_loglevelBox_currentIndexChanged);

    connect(printButton, &QPushButton::clicked, this,
        &LogScreen::printConfig_clicked);

    connect(printTagButton, &QPushButton::clicked, this,
        &LogScreen::printTagConfig_clicked);

    // link to logging 

    s_textEdit = textEdit;
    qInstallMessageHandler(myMessageOutput);
}

LogScreen::~LogScreen() {}


void LogScreen::on_loglevelBox_currentIndexChanged(int index)
{
  log_set_level(index);
  log_level = index;
  //debug_level = index + LOG_LVL_SILENT;
}

// Save the log window contents as a text file

void LogScreen::on_logsaveButton_clicked()
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
      stream << textEdit->toPlainText();
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

void LogScreen::append(QString string) {
    textEdit->append(string);
}

void LogScreen::clear() {
    textEdit->clear();
}