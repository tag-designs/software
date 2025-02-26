#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPromise>
#include "tag.pb.h"

class QWidget;
class QTextEdit;
class QGroupBox;
class ConfigTab;
class Tag;

extern QTextEdit *s_textEdit;
class LogScreen;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:

  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
 

signals:

  void StateUpdate(TagState state); 
  //void TagAttach(const Config &config);
  //void TagDetach();

private:

  // log tab events
 
  void on_LogConfigButton_clicked();    // log current configuration (from UI)
  void on_LogTagConfigButton_clicked(); // log current tag configuration
  
  void SetConfigFromTag();
  void Start();

  void DataDownloadHelper(QPromise<void> &promise,std::fstream &);

private slots:

  // attach/detach to tag

  bool Attach();

  // timer tick

  void TriggerUpdate();

  // control tab events

  void on_syncButton_clicked();     // synchronize clock
  void on_stopButton_clicked();     // stop tag
 
  void on_eraseButton_clicked();    // reset tag flash
  void on_testButton_clicked();     // run tag self-test

  void on_Attach_clicked();
  void on_Detach_clicked();

  // data events

  void on_internalDownloadButton_clicked(); // download data
  //void on_externalDownloadButton_clicked();

 

private:
  Tag tag;
  TagType tt = TagType::TAG_UNSPECIFIED;
  Ui::MainWindow *ui = nullptr;
  QTimer timer;
  TagState current_state = STATE_UNSPECIFIED;
  ConfigTab *configtab_;
  LogScreen *logtab;
  const float version = 2.0;
  //static void download_data(Tag &, QProgressBar &, std::fstream &,int);
protected:
};

#endif // MAINWINDOW_H
