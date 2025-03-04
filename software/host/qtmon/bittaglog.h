#ifndef BITTAGLOG_H
#define BITTAGLOG_H
#include <QWidget>
#include <QVBoxLayout>
#include <configinterface.h>

class PBEnumGroup;
class BitTagLogTab : public QWidget, public ConfigInterface
{

public:
    explicit BitTagLogTab(QWidget *parent = nullptr);
    ~BitTagLogTab();

    // Set/Get configuration

    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);

public slots:

    bool Attach(const Config &config);
    void Detach();

private:
    QVBoxLayout *vbox_ = nullptr;
    PBEnumGroup *log_ = nullptr; 
};

#endif /* BITTAGLOG_H */
