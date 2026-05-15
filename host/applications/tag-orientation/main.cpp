#include <QCoreApplication>
#include <signal.h>
#include <QDebug>
#include "MainClass.h"


void SigInt_Handler(int n_signal){
    printf("interrupted with code %u\n", n_signal); //Ctrl+C
    QCoreApplication::instance()->quit(); //exit(1);
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    for (int i = 0; i < argc; i++)
        qInfo() << argv[i];

    MainClass mainClass(QCoreApplication::instance());
    signal(SIGINT, &SigInt_Handler);
    return a.exec();
}
