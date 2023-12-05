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

class Adxl362Config : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit Adxl362Config(QWidget *parent = nullptr);
    ~Adxl362Config();

    // Set/Get configuration

    void GetConfig(Config &config);
    void SetConfig(const Config &config);
    QWidget *GetWidget() { return this; }

public slots:

    void Attach(const Config &config);
    //void Detach();
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
    Adxl362_AdxlType adxl_type = Adxl362_AdxlType_AdxlType_362;
    QLabel inact_thresh_label = QLabel("Inactive Threshold");
    
};

#endif // ADXL362CONFIG_H
