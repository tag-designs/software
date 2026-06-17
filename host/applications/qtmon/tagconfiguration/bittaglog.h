#ifndef BITTAGLOG_H
#define BITTAGLOG_H
#include <QWidget>
#include <QVBoxLayout>
#include "configinterface.h"
#include "configfieldvisibility.h"
#include "tagclass.h"

class PBEnumGroup;
class BitTagLogTab : public QWidget, public ConfigInterface
{

public:
    explicit BitTagLogTab(QWidget *parent = nullptr);
    ~BitTagLogTab();

    // Set/Get configuration

    bool GetConfig(Config &config);
    bool SetConfig(const Config &config);
    bool SetConfig(const Config &config,
                   const ConfigFieldVisibility &visibility);

public slots:

    bool Attach(Tag &tag);
    void Detach();

private:
    QVBoxLayout *vbox_ = nullptr;
    PBEnumGroup *log_ = nullptr; 
    ConfigFieldVisibility visibility_;
};

#endif /* BITTAGLOG_H */
