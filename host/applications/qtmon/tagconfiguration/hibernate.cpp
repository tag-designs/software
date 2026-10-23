/**
 * @file    hibernate.cpp
 * @brief   One hibernation row: start and end times kept in order.
 */

#include <QWidget>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QTimeZone>

#include "hibernate.h"
#include "hourtime.h"

Hibernate::Hibernate(int index, QWidget *parent) : QWidget(parent)
{
    ui.setupUi(this);
    HourTime::setUp(ui.hibernate_start);
    HourTime::setUp(ui.hibernate_end);
    id = index;
    on_hibernate_in_use_toggled(ui.hibernate_in_use->isChecked());
}

Hibernate::~Hibernate(){}

void Hibernate::on_hibernate_start_dateTimeChanged(const QDateTime &dateTime)
{
    if (HourTime::roundBox(ui.hibernate_start, dateTime, /*down=*/true))
        return;
    if (dateTime > ui.hibernate_end->dateTime())
        ui.hibernate_end->setDateTime(dateTime);
    
    emit startDateTimeChanged(id);
    
}

void Hibernate::on_hibernate_end_dateTimeChanged(const QDateTime &dateTime)
{
    if (HourTime::roundBox(ui.hibernate_end, dateTime))
        return;
    if (dateTime < ui.hibernate_start->dateTime())
    {
        ui.hibernate_start->setDateTime(dateTime);
    }  
    emit endDateTimeChanged(id);
}

void Hibernate::on_hibernate_in_use_toggled(bool checked)
{
    ui.hibernate_start->setEnabled(checked);
    ui.hibernate_end->setEnabled(checked);
}

void Hibernate::on_hibernate_in_use_clicked()
{
    emit inUseClicked(id);
}

bool Hibernate::inUse() const
{
    return ui.hibernate_in_use->isChecked();
}

void Hibernate::setInUse(bool inUse)
{
    ui.hibernate_in_use->setChecked(inUse);
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
