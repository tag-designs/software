
#ifndef CONSTANTS_DIALOG_H
#define CONSTANTS_DIALOG_H

#include <QDialog>
#include "ui_constants_dialog.h" // Include the auto-generated UI header

namespace Ui {
class ConstantsDialog;
}

class ConstantsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConstantsDialog(QWidget *parent = nullptr);
    ~ConstantsDialog();

    void SetConstants(float *V, float (*A)[3]);

private:
    Ui_Dialog *ui;
};

#endif // CALIBRATION_CONSTANTS_DIALOG_H