#ifndef ACCELTAGLOG_H
#define ACCELTAGLOG_H
#include <QWidget>
#include <QLayout>
#include <configinterface.h>

class QWidget;
class ConfigInterface;
class PBEnumGroup;
class AccelTagLog : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit AccelTagLog(QWidget *parent = nullptr);
    ~AccelTagLog();

    // Set/Get configuration

    void GetConfig(Config &config);
    void SetConfig(const Config &config);
    QWidget *GetWidget() { return this; }

public slots:

    void Attach(const Config &config);

private:
    QVBoxLayout *vbox_ = nullptr;
    PBEnumGroup *log_ = nullptr;
};

#endif /* ACCELTAGLOG_H */
