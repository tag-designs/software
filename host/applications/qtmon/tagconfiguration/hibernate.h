/**
 * @file    hibernate.h
 * @brief   One hibernation row of the schedule panel: a start and an end time.
 */

#ifndef HIBERNATE_H
#define HIBERNATE_H

class QWidget;
class QDateTimeEdit;

#include "ui_hibernate.h"

/**
 * @class   Hibernate
 * @brief   Edits one `Config.hibernate` interval, in UTC.
 *
 * @details The end is kept at or after the start: moving either past the
 *          other drags the other with it. SchedulePanel builds two of these
 *          once and shows as many as the tag reports.
 */
class Hibernate : public QWidget
{
   Q_OBJECT
public:
    /**
     * @param[in] index   Row number, reported by the change signals.
     * @param[in] parent  Qt parent.
     */
    explicit Hibernate(int index, QWidget *parent = nullptr);
    ~Hibernate();

    /// Copies the start time into @p dateTime.
    void start_dateTime(QDateTime &dateTime);
    /// Copies the end time into @p dateTime.
    void end_dateTime(QDateTime &dateTime);
    /// Sets the start time, in seconds since the Unix epoch.
    void setStartEpoch(int32_t epoch);
    /// Sets the end time, in seconds since the Unix epoch.
    void setEndEpoch(int32_t epoch);
    /// The start time, in seconds since the Unix epoch.
    int32_t StartEpoch();
    /// The end time, in seconds since the Unix epoch.
    int32_t EndEpoch();

public slots:

    /**
     * @brief Moves the start earlier to @p dateTime if it is currently later.
     * @note  Despite its name, this sets no minimum on the control.
     */
    void set_minimum_start_dateTime(const QDateTime &dateTime);

signals: 

    /// The start time changed; the argument is this row's index.
    void startDateTimeChanged(int);
    /// The end time changed; the argument is this row's index.
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
