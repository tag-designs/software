#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include "ui_logwindow.h"
#include "tagclass.h"

class LogWindow : public QWidget
{
    Q_OBJECT

    public:
        explicit LogWindow(QWidget *parent = nullptr);
        ~LogWindow();

    public slots:

        bool Attach(Tag &tag);

    private slots:

        void on_logsaveButton_clicked();
        void on_loglevelBox_currentIndexChanged(int);

    private:

        Ui::LogWindow ui;
};

#endif