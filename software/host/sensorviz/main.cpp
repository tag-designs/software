#include <QApplication>
#include <QTextEdit>

#include "mainwindow.h"
#include "../qthoststyle.h"

extern QTextEdit *s_textEdit;

void sensorVizMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (!s_textEdit) {
        return;
    }

    QString tag;
    switch (type) {
    case QtDebugMsg:
        tag = "DEBUG";
        break;
    case QtInfoMsg:
        tag = "INFO";
        break;
    case QtWarningMsg:
        tag = "WARN";
        break;
    case QtCriticalMsg:
        tag = "ERROR";
        break;
    case QtFatalMsg:
        abort();
    }

    if (context.line) {
        s_textEdit->append(QString("%1 %2:%3 %4")
                               .arg(tag)
                               .arg(context.file ? context.file : "")
                               .arg(context.line)
                               .arg(msg));
    } else {
        s_textEdit->append(QString("%1 %2").arg(tag, msg));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    HostStyle::apply(app);
    qInstallMessageHandler(sensorVizMessageOutput);
    MainWindow window;
    window.show();
    return app.exec();
}
