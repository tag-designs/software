#ifndef PRESTAGLOG_H
#define PRESTAGLOG_H
#include <QWidget>
#include <configinterface.h>

class QWidget;
class ConfigInterface;
class PBEnumGroup;
class PresTagLogTab : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit PresTagLogTab(QWidget *parent = nullptr);
    ~PresTagLogTab();

    // Set/Get configuration

    void GetConfig(Config &config);
    void SetConfig(const Config &config);
    QWidget *GetWidget() { return this; }

public slots:

    void Attach(const Config &config);

private:
    QVBoxLayout *vbox_ = nullptr;
    QGroupBox *spinner_ = nullptr;
    QSpinBox *period = nullptr;
};

#endif /* BITTAGLOG_H */
