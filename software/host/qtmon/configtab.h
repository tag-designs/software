#ifndef CONFIG_H
#define CONFIG_H

#include <QWidget>
//#include <QDateTime>
#include <QTabWidget>
#include <QPushButton>
#include <QList>
#include "tag.pb.h"
//#include "host.pb.h"
//#include "tagclass.h"
#include "schedule.h"
#include "adxl362config.h"
#include "bittaglog.h"
#include "dataconfig.h"


class ConfigTab :  public QTabWidget, public ConfigInterface {
    Q_OBJECT

public:
    explicit ConfigTab(QWidget *parent = nullptr);
    ~ConfigTab();
    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
    bool isActive();

public slots:

    bool Attach(const Config &config);
    void Detach();
    void StateUpdate(TagState state);

private:

    // Helper function

    TagType tag_type_ = TAG_UNSPECIFIED;
    TagState old_state_ = STATE_UNSPECIFIED;
    // Schedule tab and components
    QWidget scheduleTab;
    Schedule schedule;
    BitTagLogTab btlog;
    // Sensor tab and components
    QWidget sensorTab;
    Adxl362Config adxl;
    bool active = false;
    
};

#endif // CONFIG_H
