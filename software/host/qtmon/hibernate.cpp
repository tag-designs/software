#include <QWidget>
#include <QDateTimeEdit>
#include <QFormLayout>

#include "hibernate.h"

Hibernate::Hibernate(int index, QWidget *parent) : QWidget(parent)
{
    ui.setupUi(this);
    id = index;
}

Hibernate::~Hibernate(){}

void Hibernate::on_hibernate_start_dateTimeChanged(const QDateTime &dateTime)
{
    if (dateTime > ui.hibernate_end->dateTime())
        ui.hibernate_end->setDateTime(dateTime);
    
    emit startDateTimeChanged(id);
    
}

void Hibernate::on_hibernate_end_dateTimeChanged(const QDateTime &dateTime)
{
    if (dateTime < ui.hibernate_start->dateTime())
    {
        ui.hibernate_start->setDateTime(dateTime);
    }  
    emit endDateTimeChanged(id);
}

void Hibernate::set_minimum_start_dateTime(const QDateTime &dateTime)
{
    if (dateTime < ui.hibernate_start->dateTime())
    {
        ui.hibernate_start->setDateTime(dateTime);
    }
}

void Hibernate::start_dateTime(QDateTime &dateTime)
{
    dateTime = ui.hibernate_start->dateTime();
}

void Hibernate::end_dateTime(QDateTime &dateTime)
{
    dateTime = ui.hibernate_end->dateTime();
}

void Hibernate::setStartEpoch(int32_t epoch) {
    ui.hibernate_start->setDateTime(QDateTime::fromSecsSinceEpoch(epoch));
}

void Hibernate::setEndEpoch(int32_t epoch) {
    ui.hibernate_end->setDateTime(QDateTime::fromSecsSinceEpoch(epoch));
}

int32_t Hibernate::StartEpoch() {
    return (int32_t) ui.hibernate_start->dateTime().toSecsSinceEpoch();
}

int32_t Hibernate::EndEpoch() {
    return (int32_t) ui.hibernate_end->dateTime().toSecsSinceEpoch();
}