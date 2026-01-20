
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

void ConstantsDialog::SetConstants(float *V, float (*A)[3]){
    ui->a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
    ui->a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
    ui->a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

    ui->v0Label->setText(QString::asprintf("%+.3f",V[0]));
    ui->v1Label->setText(QString::asprintf("%+.3f",V[1]));
    ui->v2Label->setText(QString::asprintf("%+.3f",V[2]));
}