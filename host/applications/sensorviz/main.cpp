#include <QApplication>
#include <QCommandLineParser>
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
    QCoreApplication::setApplicationName(QStringLiteral("sensorViz"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SQLite sensor log visualization tool."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption load_log_option(
        QStringLiteral("load-log"),
        QStringLiteral("Load a SQLite sensor log after startup."),
        QStringLiteral("path"));
    QCommandLineOption load_preferences_option(
        QStringLiteral("load-preferences"),
        QStringLiteral("Load a sensorViz display preferences JSON file."),
        QStringLiteral("path"));
    QCommandLineOption capture_screenshot_option(
        QStringLiteral("capture-screenshot"),
        QStringLiteral("Capture one named documentation screenshot and exit."),
        QStringLiteral("name"));
    QCommandLineOption capture_suite_option(
        QStringLiteral("capture-suite"),
        QStringLiteral("Capture a documentation screenshot suite and exit."),
        QStringLiteral("suite"));
    QCommandLineOption screenshot_dir_option(
        QStringLiteral("screenshot-dir"),
        QStringLiteral("Directory where generated PNG screenshots are written."),
        QStringLiteral("dir"));
    QCommandLineOption no_user_prompts_option(
        QStringLiteral("no-user-prompts"),
        QStringLiteral("Report automation failures without opening modal prompts."));

    parser.addOption(load_log_option);
    parser.addOption(load_preferences_option);
    parser.addOption(capture_screenshot_option);
    parser.addOption(capture_suite_option);
    parser.addOption(screenshot_dir_option);
    parser.addOption(no_user_prompts_option);
    parser.process(app);

    HostStyle::apply(app);
    qInstallMessageHandler(sensorVizMessageOutput);

    MainWindowOptions options;
    options.loadLogPath = parser.value(load_log_option);
    options.loadPreferencesPath = parser.value(load_preferences_option);
    options.captureScreenshot = parser.value(capture_screenshot_option);
    options.captureSuite = parser.value(capture_suite_option);
    options.screenshotDir = parser.value(screenshot_dir_option);
    options.noUserPrompts = parser.isSet(no_user_prompts_option);

    MainWindow window(options);
    window.show();
    return app.exec();
}
