/**
 * @file    custommessagebox.h
 * @brief   Tag Programmer's progress dialog, which cannot be closed while it works.
 */


#ifndef CUSTOMMESSAGEBOX_H
#define CUSTOMMESSAGEBOX_H

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>

/**
 * @class   CustomMessageBox
 * @brief   The progress dialog shown while a tag is programmed.
 *
 * @details A message, an optional progress bar (the tag's erase, in sectors),
 *          and OK and Cancel. OK, or start(), disables both buttons and emits
 *          OkButtonClicked(), which the caller connects to the work; from then
 *          on the dialog can only be closed by the caller (done()), so the user
 *          cannot dismiss it mid-erase. Cancel before starting closes it.
 */
class CustomMessageBox : public QDialog {
public:
    /**
     * @param[in] text    Initial message.
     * @param[in] parent  Qt parent.
     */
    CustomMessageBox(const QString& text, QWidget *parent = nullptr);

    Q_OBJECT


signals:

    /// The work should start: OK was pressed, or start() was called.
    void OkButtonClicked();

public slots:

    /// Replaces the message, e.g. with the current programming step.
    void setText(QString text);
    /// Sets the progress bar's maximum; ignored unless positive.
    void setMaximum(int max);
    /**
     * @brief Shows the progress bar at @p value, or hides it once @p value
     *        reaches the maximum (the erase is done).
     */
    void setValue(int value);
    /// Starts without waiting for OK, for a caller that has already confirmed.
    void start() { onOkButtonClicked(); }

private slots:
    
    /// Cancel: allows the close and closes the dialog.
    void onCancelButtonClicked();
    /// OK: disables both buttons and emits OkButtonClicked().
    void onOkButtonClicked();
    /// Ignores the window's close button unless Cancel allowed it.
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel* messageLabel;         ///< The current step's text.
    QPushButton* okButton;        ///< Starts the work.
    QPushButton* cancelButton;    ///< Closes the dialog before the work starts.
    QProgressBar* progressBar;    ///< The tag's erase, in sectors; hidden until setValue().
    bool closeButtonClicked = false;  ///< Cancel was pressed, so closeEvent() lets the dialog close.
};

#endif