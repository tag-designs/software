
#ifndef COMPASS_CALIBRATION_DIALOG_H
#define COMPASS_CALIBRATION_DIALOG_H

#include <QDialog>
#include "compass_types.h"

class Ui_Dialog;

class CompassCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompassCalibrationDialog(QWidget *parent = nullptr);
    ~CompassCalibrationDialog();

    // The dialog is read-only: it gives the user visibility into the calibration
    // constants read from the log but does not edit or save them.
    void setConstants(const CompassCalibration &calibration);

private:
    Ui_Dialog *ui;
};

#endif // COMPASS_CALIBRATION_DIALOG_H
