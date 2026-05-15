#include "mainwindow.h"
#include <QApplication>
#include "qthoststyle.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    HostStyle::apply(a);
    MainWindow w;
    w.show();

    return a.exec();
}
