#ifndef LSM6DSVCONFIG_H
#define LSM6DSVCONFIG_H

#include <QWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include "pbenumgroup.h"
#include "tag.pb.h"
#include "tagclass.h" 

#include "configinterface.h"
#include "configfieldvisibility.h"

class Lsm6dsvConfig : public QWidget, public ConfigInterface
{
    Q_OBJECT
    //Q_INTERFACES(ConfigInterface)

public:
    explicit Lsm6dsvConfig(QWidget *parent = nullptr);
    ~Lsm6dsvConfig();

    // Set/Get configuration

    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
    bool SetConfig(const Config &config,
                   const ConfigFieldVisibility &visibility);

public slots:

    bool Attach(Tag &tag);
    void Detach(){};
    //void StateUpdate(State_TagState state);

private slots:

private:
    QVBoxLayout *vbox_ = nullptr;
    QGroupBox *configbox_ = nullptr;
    PBEnumGroup *odr_ = nullptr;
    PBEnumGroup *accel_range_ = nullptr;
    PBEnumGroup *gyro_range_ = nullptr;
    ConfigFieldVisibility visibility_;
};

#endif // LSM6DSVCONFIG_H
