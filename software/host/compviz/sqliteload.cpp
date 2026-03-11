#include <QtCore>
#include <QtGui>
#include <QVector>
#include <QPrinter>
#include <QDebug>
#include <QTextStream>
#include <QtSql>
#include <QSqlDatabase>
#include <qcustomplot.h>
#include <iostream>
#include <float.h>
#include 

#include "mainwindow.h"
#include "ui_mainwindow.h"

void MainWindow::on_pb_load_clicked()
{
    QMessageBox msgBox;
    QSqlDatabase db =  QSqlDatabase::addDatabase("QSQLITE");
    QSqlQuery query(db);

    int i;

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Data"), path,
                                                    tr("Data Files (*.db3)"));
    if (fileName.isNull())
        return;

    db.setDatabaseName(fileName);
    if (db.open()){
        qDebug() << "Database connected successfully";
    } else {
        qDebug() << "Couldn't open database file " << p <<  " error: " << db.lastError().text();
        return;
    }

    // Load information table

    query.exec("SELECT (fieldname, value) FROM info");
   
    while (query.next()){
        QString fieldname = query.value(0).toString();
        QString value = query.value(1).toString();

        switch (fieldname) {
            case: "tagtype":
                break;
            case: "info":
                break;
            case: "config":
                break;
            case: "calibration":
                break;
        }
    }



    // Load the config string and check tag type

    // Load the calibration constants

    // Load temperature

    // Load voltage

    // Load Activity

    // Load Compass Data


}
