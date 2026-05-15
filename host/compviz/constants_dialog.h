
#ifndef CONSTANTS_DIALOG_H
#define CONSTANTS_DIALOG_H

#include <QDialog>
#include "ui_constants_dialog.h" // Include the auto-generated UI header
#include "compass_types.h"

namespace Ui {
class ConstantsDialog;
}

class ConstantsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConstantsDialog(QWidget *parent = nullptr);
    ~ConstantsDialog();

    // The dialog is read-only: it gives the user visibility into the calibration
    // constants read from the log but does not edit or save them.
    void setConstants(const CompassCalibration &calibration);

private:
    Ui_Dialog *ui;
};

#endif // CALIBRATION_CONSTANTS_DIALOG_H
