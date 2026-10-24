/**
 * @file    logwindow.h
 * @brief   Error Log tab for qtmonitor.
 *
 * @details LogWindow shows Qt and tagcore log messages in a read-only text
 *          box. Its commands (save, clear, log level) are actions, which
 *          MainWindow places in the File and Log menus and which also appear
 *          on the text box's popup menu. It provides explicit size hints so
 *          qtmonitor and documentation screenshot capture can size the tab
 *          without clipping the log text.
 */

#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include "ui_logwindow.h"
#include "tagclass.h"

class QAction;
class QActionGroup;
class QMenu;
class QPoint;
class QSize;

/**
 * @class   LogWindow
 * @brief   The Error Log tab: Qt and tagcore messages, with save, clear and
 *          log-level actions.
 *
 * @details Construction installs the Qt message handler and points the
 *          tagcore log callback's text box (s_textEdit) at this tab, so only
 *          one LogWindow should exist. The log level starts at INFO.
 */
class LogWindow : public QWidget
{
    Q_OBJECT

    public:
        /**
         * @brief Builds the tab and its actions, and starts capturing messages.
         * @param[in] parent  Qt parent.
         */
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
         * @return Minimum size that keeps the log pane usable.
         */
        QSize minimumSizeHint() const override;

        /// "Save Error Log…": writes the log to a text file the user chooses.
        QAction *saveAction() const { return save_; }
        /// "Clear Error Log": empties the log.
        QAction *clearAction() const { return clear_; }
        /**
         * @brief "Log Level" submenu: one checkable action per level, TRACE
         *        to ERROR, exclusive; INFO at startup.
         */
        QMenu *logLevelMenu() const { return level_menu_; }

    public slots:
        /**
         * @brief Called when a tag is attached; the log needs nothing from it.
         * @return Always true.
         */
        bool Attach(Tag &tag);

    private slots:
        /// Save Error Log…: writes the log's text to a file the user chooses.
        void saveLog();
        /// Clear Error Log: empties the log.
        void clearLog();
        /// Applies a level chosen in the Log Level menu.
        void setLogLevel(QAction *level);
        /// The text box's popup menu: Qt's copy actions plus the log actions.
        void showContextMenu(const QPoint &pos);

    private:
        Ui::LogWindow ui;                 ///< The Designer-built widgets (logwindow.ui).
        QAction *save_ = nullptr;         ///< See saveAction().
        QAction *clear_ = nullptr;        ///< See clearAction().
        QMenu *level_menu_ = nullptr;     ///< See logLevelMenu().
        QActionGroup *levels_ = nullptr;  ///< Makes the level actions exclusive.
};

#endif
