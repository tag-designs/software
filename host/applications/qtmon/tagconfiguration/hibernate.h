#ifndef HIBERNATE_H
#define HIBERNATE_H

class QWidget;
class QDateTimeEdit;

#include "ui_hibernate.h"

class Hibernate : public QWidget
{
   Q_OBJECT
public:
    explicit Hibernate(int index, QWidget *parent = nullptr);
    ~Hibernate();

    void start_dateTime(QDateTime &dateTime);
    void end_dateTime(QDateTime &dateTime);
    void setStartEpoch(int32_t epoch);
    void setEndEpoch(int32_t epoch);
    int32_t StartEpoch();
    int32_t EndEpoch();

public slots:

    void set_minimum_start_dateTime(const QDateTime &dateTime);

signals: 

    void startDateTimeChanged(int);
    void endDateTimeChanged(int);

private slots:
    void on_hibernate_start_dateTimeChanged(const QDateTime &dateTime);
    void on_hibernate_end_dateTimeChanged(const QDateTime &dateTime);

private:
    Ui::Hibernate ui;
    /*
    QDateTimeEdit *hibernate_start;
    QDateTimeEdit *hibernate_end;
    */
    int id;
};

#endif // HIBERNATE_H
