#ifndef BITTAGLOG_H
#define BITTAGLOG_H
#include <QWidget>
#include <configinterface.h>

class QWidget;
class ConfigInterface;
class PBEnumGroup;
class BitTagLogTab : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit BitTagLogTab(QWidget *parent = nullptr);
    ~BitTagLogTab();

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

#endif /* BITTAGLOG_H */
