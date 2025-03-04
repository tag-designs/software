#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <QWidget>
#include <QList>
#include <configinterface.h>

//#include "tag.pb.h"
//#include "host.pb.h"
//#include "tagclass.h"

#include "hibernate.h"
#include "ui_schedule.h"

class Schedule : public QWidget, public ConfigInterface
{

    public:
        Schedule(QWidget *parent = nullptr);
        ~Schedule();

        bool SetConfig(const Config &config);
        bool GetConfig(Config &config);

    public slots:

        bool Attach(const Config &config);
        void Detach();

    private slots:

        // start/end configuration

        void on_StartDateTime_dateTimeChanged(const QDateTime &dateTime);
        void on_EndDateTime_dateTimeChanged(const QDateTime &dateTime);
        void on_startGroup_clicked();
        void on_runGroup_clicked();

    private:

        QList<Hibernate *> hibernate_list_;
        Ui::Schedule ui;

};

#endif // SCHEDULE_H
