
#ifndef DATACONFIG_H
#define DATACONFIG_H

#include <configinterface.h>

class QWidget;
class ConfigInterface;
class PBEnumGroup;
class DataConfig : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit DataConfig(QWidget *parent = nullptr);
    ~DataConfig();

    // Set/Get configuration

    void GetConfig(Config &config);
    void SetConfig(const Config &config);
    QWidget *GetWidget(){ return this;}

public slots:

    void Attach(const Config &config);

private:
    void AddConfigItem(ConfigInterface *, const Config &config);
    QVBoxLayout *vbox_ = nullptr;
    QList<ConfigInterface *> configlist_;
    //PBEnumGroup *internal_log_ = nullptr;
    //PBEnumGroup *external_log_ = nullptr;
};

#endif /* DATACONFIG_H */
