
#include <QString>
#include "constants_dialog.h"

ConstantsDialog::ConstantsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui_Dialog)
{
    ui->setupUi(this); // This sets up all widgets and layouts
}

ConstantsDialog::~ConstantsDialog(){
    delete ui;
}

void ConstantsDialog::SetConstants(float *H, float (*S)[3]){
    ui->a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", S[0][0],S[0][1],S[0][2]));
    ui->a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", S[1][0],S[1][1],S[1][2]));
    ui->a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", S[2][0],S[2][1],S[2][2]));

    ui->v0Label->setText(QString::asprintf("%+.3f",H[0]));
    ui->v1Label->setText(QString::asprintf("%+.3f",H[1]));
    ui->v2Label->setText(QString::asprintf("%+.3f",H[2]));
}