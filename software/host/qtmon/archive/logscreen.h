#ifndef LOG_SCREEN_H
#define LOG_SCREEN_H

class QWidget;
class QTextEdit;
class QComboBox;
class QPushButton;
class QLayout;

class LogScreen : public QWidget
{
    Q_OBJECT

public:

    LogScreen(QWidget *parent = nullptr);
    ~LogScreen();

    void append(QString string);
    void clear();

signals:

    void printConfig_clicked();
    void printTagConfig_clicked();

private slots: 

    void on_logsaveButton_clicked();
    void on_loglevelBox_currentIndexChanged(int index);

private:

    QVBoxLayout *vbox;
    QTextEdit *textEdit;
    QComboBox *loglevelBox;
    QPushButton *saveButton;
    QPushButton *printButton;
    QPushButton *printTagButton;

};

#endif