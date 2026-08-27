/**
 * @file    logwindow.h
 * @brief   Error Log tab for qtmonitor.
 *
 * @details LogWindow owns the user-facing error log controls and provides
 *          explicit size hints so qtmonitor and documentation screenshot
 *          capture can size the tab without clipping the log text edit.
 */

#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include "ui_logwindow.h"
#include "tagclass.h"

class QSize;

class LogWindow : public QWidget
{
    Q_OBJECT

    public:
        explicit LogWindow(QWidget *parent = nullptr);
        ~LogWindow();
        /**
         * @brief Returns the preferred size for the Error Log tab.
         *
         * @details The default QTextEdit hint is not representative of the
         *          compact tab size needed by qtmonitor and documentation
         *          screenshots, so the tab provides an explicit preferred size.
         *
         * @return Preferred LogWindow size in pixels.
         */
        QSize sizeHint() const override;
        /**
         * @brief Returns the smallest useful Error Log tab size.
         *
         * @return Minimum size that keeps the toolbar and log pane usable.
         */
        QSize minimumSizeHint() const override;

    public slots:

        bool Attach(Tag &tag);

    private slots:

        void on_logsaveButton_clicked();
        void on_logclearButton_clicked();
        void on_loglevelBox_currentIndexChanged(int);

    private:

        Ui::LogWindow ui;
};

#endif
