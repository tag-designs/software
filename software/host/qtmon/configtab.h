#ifndef CONFIG_H
#define CONFIG_H

#include <QWidget>
//#include <QDateTime>
#include <QTabWidget>
#include <QPushButton>
#include <QList>
#include <QMessageBox>
//#include "host.pb.h"
//#include "tagclass.h"
#include "schedule.h"
#include "adxl362config.h"
#include "bittaglog.h"
#include "ui_configtab.h"
#include "tagclass.h"

class ConfigTab :  public QWidget {
    Q_OBJECT

public:
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();
    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
    bool isActive();

public slots:

    bool Attach(Tag &tag);
    void Detach();
    void StateUpdate(TagState state);

private slots:

    void on_configSaveButton_clicked();
    void on_configRestoreButton_clicked();
    void on_startButton_clicked();
    void on_readButton_clicked();

private:

    // Helper function

    TagType tag_type_ = TAG_UNSPECIFIED;
    TagState old_state_ = STATE_UNSPECIFIED;

    // Schedule tab and components

    //QWidget scheduleTab;

    Schedule schedule;
    BitTagLogTab btlog;

    // Sensor tab and components

    //QWidget sensorTab;

    Adxl362Config adxl;
    bool active = false;

    // for errors

    QMessageBox msgBox;
    Tag *tag;
    Ui::ConfigTab ui;
    
};

#endif // CONFIG_H
