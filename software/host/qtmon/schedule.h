#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <QWidget>
#include <QList>
#include <configinterface.h>

//#include "tag.pb.h"
//#include "host.pb.h"
#include "tagclass.h"

#include "hibernate.h"
#include "ui_schedule.h"

class Schedule : public QWidget, public ConfigInterface
{

    public:
        Schedule(QWidget *parent = nullptr);
        ~Schedule();
        
        //! @copydoc ConfigInterface::SetConfig(const Config &)
        bool SetConfig(const Config &config);

        //! @copydoc ConfigInterface::GetConfig(Config &)
        bool GetConfig(Config &config);

    public slots:

        //! @copydoc ConfigInterface::Attach(const Tag &)
        bool Attach(Tag &tag);

        //! @copydoc ConfigInterface::Detach()
        void Detach();

    private slots:

        // start/end configuration -- slots tied to UI

        void on_StartDateTime_dateTimeChanged(const QDateTime &dateTime);
        void on_EndDateTime_dateTimeChanged(const QDateTime &dateTime);
        void on_startGroup_clicked();
        void on_runGroup_clicked();

    private:

        QList<Hibernate *> hibernate_list_;
        Ui::Schedule ui;

};

#endif // SCHEDULE_H
