#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <QWidget>
#include <QList>
#include <configinterface.h>
#include "tag.pb.h"
//#include "host.pb.h"
//#include "tagclass.h"
#include "hibernate.h"

namespace Ui
{
    class Schedule;
}

class Schedule : public QWidget, public ConfigInterface
{
    Q_OBJECT
    Q_INTERFACES(ConfigInterface)

public:
    explicit Schedule(QWidget *parent = nullptr);
    ~Schedule();

    void SetConfig(const Config &config);
    void GetConfig(Config &config);
    QWidget *GetWidget(){ return this;}

public slots:

    void Attach(const Config &config);

private slots:

    // start/end configuration

    void on_StartDateTime_dateTimeChanged(const QDateTime &dateTime);
    void on_EndDateTime_dateTimeChanged(const QDateTime &dateTime);
    void on_startGroup_clicked();
    void on_runGroup_clicked();

private:

    Ui::Schedule *ui_;
    QList<Hibernate *> hibernate_list_;

};

#endif // SCHEDULE_H
