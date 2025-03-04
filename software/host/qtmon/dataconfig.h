
#ifndef DATACONFIG_H
#define DATACONFIG_H

#include <configinterface.h>
#include <QVBoxLayout>

class QWidget;
class ConfigInterface;
class PBEnumGroup;
class DataConfig : public QWidget, public ConfigInterface
{

public:
    explicit DataConfig(QWidget *parent = nullptr);
    ~DataConfig();

    // Set/Get configuration

    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
    
public slots:

    bool Attach(const Config &config);
    void Detach(){};

private:
    void AddConfigItem(QWidget *, const Config &config);
    QVBoxLayout *vbox_ = nullptr;
    QList<QWidget *> configlist_;
};

#endif /* DATACONFIG_H */
