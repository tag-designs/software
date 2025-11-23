#include <QApplication>
#include <QWidget>
#include <QtQuickWidgets/QQuickWidget>



#include "mainwindow.h"
#include <QtQuickWidgets/QQuickWidget>
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{

    ui->setupUi(this);
    ui->tagWidget->setSource(QUrl("qrc:/orientation.qml"));
    rootObject = ui->tagWidget->rootObject();
    rotateImage(QQuaternion(sqrt(2)/2,0,sqrt(2)/2,0));
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::rotateImage(QQuaternion qt){
    QMetaObject::invokeMethod(rootObject, "setRotationQuaternion",
                              //Q_RETURN_ARG(QString, returnedValue),
                              Q_ARG(QVariant, qt));
}
