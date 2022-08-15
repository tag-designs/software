#include <QWidget>
#include <QDateTimeEdit>
#include <QFormLayout>

#include "hibernate.h"

Hibernate::Hibernate(int index, QWidget *parent) : QWidget(parent)
{
    id = index;

    QFormLayout *layout = new QFormLayout;
    hibernate_start = new QDateTimeEdit;
    hibernate_end = new QDateTimeEdit;

    hibernate_start->setCalendarPopup(true);
    hibernate_start->setTimeSpec(Qt::TimeSpec::UTC);
    hibernate_start->setDisplayFormat("M/d/yy h:00 AP");
    hibernate_end->setCalendarPopup(true);
    hibernate_end->setTimeSpec(Qt::TimeSpec::UTC);
    hibernate_end->setDisplayFormat("M/d/yy h:00 AP");

    layout->addRow("Start", hibernate_start);
    layout->addRow("End", hibernate_end);

    QDateTime current = QDateTime::currentDateTimeUtc();
    QTime time = current.time();

    if (time.minute() != 0) {
        time.setHMS(time.hour() + 1, 0, 0, 0);
        current.setTime(time);
    }

    hibernate_start->setDateTime(current);
    hibernate_end->setDateTime(current);
    hibernate_start->setMinimumDateTime(current);
    hibernate_end->setMinimumDateTime(current);
    hibernate_start->setToolTip("Start hibernating at date/time (UTC)");
    hibernate_end->setToolTip("Stop hibernating at data/time (UTC)");

    setLayout(layout);
    connect(hibernate_start, &QDateTimeEdit::dateTimeChanged,
            this, &Hibernate::on_hibernate_start_dateTimeChanged);

    connect(hibernate_end, &QDateTimeEdit::dateTimeChanged,
            this, &Hibernate::on_hibernate_end_dateTimeChanged);
}

Hibernate::~Hibernate(){}

void Hibernate::on_hibernate_start_dateTimeChanged(const QDateTime &dateTime)
{
    if (dateTime > hibernate_end->dateTime())
        hibernate_end->setDateTime(dateTime);
    
    emit startDateTimeChanged(id);
    
}

void Hibernate::on_hibernate_end_dateTimeChanged(const QDateTime &dateTime)
{
    if (dateTime < hibernate_start->dateTime())
    {
        hibernate_start->setDateTime(dateTime);
    }  
    emit endDateTimeChanged(id);
}

void Hibernate::set_minimum_start_dateTime(const QDateTime &dateTime)
{
    if (dateTime < hibernate_start->dateTime())
    {
        hibernate_start->setDateTime(dateTime);
    }
}

void Hibernate::start_dateTime(QDateTime &dateTime)
{
    dateTime = hibernate_start->dateTime();
}

void Hibernate::end_dateTime(QDateTime &dateTime)
{
    dateTime = hibernate_end->dateTime();
}

void Hibernate::setStartEpoch(int32_t epoch) {
    hibernate_start->setDateTime(QDateTime::fromSecsSinceEpoch(epoch));
}

void Hibernate::setEndEpoch(int32_t epoch) {
    hibernate_end->setDateTime(QDateTime::fromSecsSinceEpoch(epoch));
}

int32_t Hibernate::StartEpoch() {
    return (int32_t) hibernate_start->dateTime().toSecsSinceEpoch();
}

int32_t Hibernate::EndEpoch() {
    return (int32_t) hibernate_end->dateTime().toSecsSinceEpoch();
}