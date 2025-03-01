#ifndef SENSORS_H
#define SENSORS_H
#include <QWidget>
#include <configinterface.h>
#include <QVBoxLayout>
#include <adxl362config.h>
#include <QScrollArea>

/*
 * Class for handling the configuration of sensors
 * currently handles one sensor type -- adxl362
 * we should split this into adxl362,adxl367 as separate files
 * 
 * Learns which sensors are present by reading config passed in Attach()
 *
*/

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
    QScrollArea scroll;
    QFrame inner;
    Adxl362Config *adxlconfig_ = nullptr;
};

#endif // SENSORS_H
