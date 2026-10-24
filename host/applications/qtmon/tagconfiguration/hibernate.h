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
 *
 *          An **In use** checkbox says whether the row applies. Unchecked,
 *          the date pickers are disabled and keep whatever they show; the
 *          panel then sends the row as 0 to 0 (1 January 1970), which never
 *          applies. Checking the box enables the pickers but changes no date
 *          itself: inUseClicked() lets the panel fill in a starting date.
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

    /**
     * @brief   Whether the row is in use, i.e. its checkbox is checked.
     * @return  true if the row's dates apply, false if the row is unused.
     */
    bool inUse() const;

    /**
     * @brief   Checks or unchecks the row's checkbox, enabling or disabling
     *          the date pickers to match.
     * @details Leaves the dates alone and does not emit inUseClicked(), so
     *          a load sets the dates and the checkbox independently.
     * @param[in] inUse  true to use the row, false to leave it unused.
     */
    void setInUse(bool inUse);

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
    /**
     * @brief The user clicked the In use checkbox; the argument is this
     *        row's index. Not emitted by setInUse().
     */
    void inUseClicked(int);

private slots:
    /// Rounds the start down to the hour, keeps the end at or after it, and emits startDateTimeChanged().
    void on_hibernate_start_dateTimeChanged(const QDateTime &dateTime);
    /// Rounds the end up to the hour, keeps the start at or before it, and emits endDateTimeChanged().
    void on_hibernate_end_dateTimeChanged(const QDateTime &dateTime);
    /// Enables the date pickers when the row is in use, disables them when not.
    void on_hibernate_in_use_toggled(bool checked);
    /// Reports a user click on the checkbox as inUseClicked().
    void on_hibernate_in_use_clicked();

private:
    Ui::Hibernate ui;  ///< The In use checkbox and the start and end boxes.
    /*
    QDateTimeEdit *hibernate_start;
    QDateTimeEdit *hibernate_end;
    */
    int id;  ///< Row index, reported by the change signals.
};

#endif // HIBERNATE_H
