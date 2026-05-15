#include <QApplication>
#include <QTextEdit>

#include "mainwindow.h"
#include "qthoststyle.h"

// main.cpp owns process startup only. UI construction lives in mainwindow.cpp,
// while this file installs the shared host style and the message handler that
// feeds Qt diagnostics into the File Info tab.

extern QTextEdit *s_textEdit;

// Qt messages are routed to the File Info tab so load/plot diagnostics can be
// saved with the same workflow as compViz.
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
    // Keep startup deliberately small: after QApplication and styling, the
    // MainWindow constructor owns all menu/widget creation.
    QApplication app(argc, argv);
    HostStyle::apply(app);
    qInstallMessageHandler(sensorVizMessageOutput);
    MainWindow window;
    window.show();
    return app.exec();
}
