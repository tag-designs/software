#ifndef ADXL362CONFIG_H
#define ADXL362CONFIG_H

#include <QWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include "pbenumgroup.h"
#include "tag.pb.h"
#include "tagclass.h" 

#include "configinterface.h"
#include "configfieldvisibility.h"

class Adxl362Config : public QWidget, public ConfigInterface
{
    Q_OBJECT
    //Q_INTERFACES(ConfigInterface)

public:
    explicit Adxl362Config(QWidget *parent = nullptr);
    ~Adxl362Config();

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

    void on_adxlrange_clicked(int index);
    void on_adxlfreq_clicked(int index);

private:
    QVBoxLayout *vbox_ = nullptr;
    QGroupBox *configbox_ = nullptr;
    PBEnumGroup *sample_rate_ = nullptr;
    PBEnumGroup *range_ = nullptr;
    PBEnumGroup *filter_ = nullptr;
   
    QGroupBox *spinners_ = nullptr;
    QDoubleSpinBox *act_thresh_ = nullptr;
    QDoubleSpinBox *inact_thresh_ = nullptr;
    QDoubleSpinBox *inactive_ = nullptr;
    QLabel act_thresh_label = QLabel("Active Threshold");
    QLabel inact_thresh_label = QLabel("Inactive Threshold");
    QLabel inactive_label = QLabel("Inactivity");

    bool isAdxl375 = false;
    bool isBitTagNG = false;
    ConfigFieldVisibility visibility_;
    
};

#endif // ADXL362CONFIG_H
