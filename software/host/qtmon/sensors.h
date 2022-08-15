#ifndef SENSORS_H
#define SENSORS_H
#include <QWidget>
#include <configinterface.h>
#include <QVBoxLayout>
#include <adxl362config.h>

/* namespace Ui {
class Sensors;
} */

class Sensors : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit Sensors(QWidget *parent = nullptr);
    ~Sensors();

    void SetConfig(const Config &config);
    void GetConfig(Config &config);
    QWidget *GetWidget(){ return this;}

public slots:

    void Attach(const Config &config);
    void Detach();
    void StateUpdate(TagState state);

private:
    QVBoxLayout *vbox_ = nullptr;
    Adxl362Config *adxlconfig_ = nullptr;
};

#endif // SENSORS_H
