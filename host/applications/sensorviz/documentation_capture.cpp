/**
 * @file    documentation_capture.cpp
 * @brief   sensorViz's maintainer-only startup path that loads a log and
 *          writes user-guide screenshots, then exits.
 *
 * @details Implements MainWindow::runDocumentationStartup() and the capture
 *          functions behind the --load-log, --load-preferences,
 *          --capture-screenshot, --capture-suite and --screenshot-dir command
 *          line options parsed in main.cpp. Captures drive the live window,
 *          menus and load helpers rather than a mock, so the images track the
 *          real UI. Each screenshot is written as a PNG; see captureFileName()
 *          for how a capture name becomes a file name.
 *
 * @note    When a capture was requested the application exits with status 0
 *          only if every capture succeeded, and 1 otherwise; errors are logged
 *          with qCritical() and are not shown in a dialog in that mode.
 *
 * @warning Window captures read the screen. On Wayland, where no client may
 *          do so, they fall back to rendering the widget itself, which omits
 *          native decorations.
 */

#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <QWindow>

// Documentation capture is a maintainer-only startup path driven by command
// line options. It deliberately reuses live SensorViz actions, menus, and load
// helpers so generated screenshots track the real UI instead of a parallel mock.

namespace
{

/**
 * @brief   Directory used when --screenshot-dir is not given.
 *
 * @return  src/images under TAG_DESIGNS_DOCS_SOURCE_DIR (the user manual's
 *          source tree, defined through the host_common target), or
 *          host/docs/src/images relative to the current directory when that
 *          definition is absent. The directory is not checked or created here;
 *          the capture code creates it on demand.
 */
QString defaultScreenshotDir()
{
#ifdef TAG_DESIGNS_DOCS_SOURCE_DIR
    return QDir(QStringLiteral(TAG_DESIGNS_DOCS_SOURCE_DIR))
        .filePath(QStringLiteral("src/images"));
#else
    return QDir::current().filePath(QStringLiteral("host/docs/src/images"));
#endif
}

/**
 * @brief   Map a capture name to the PNG file name it is written to.
 *
 * @details A name already ending in ".png" (any case) is used as given, apart
 *          from trimming. Otherwise the name is lower-cased, underscores and
 *          spaces become hyphens, a "sensorviz-" prefix is added if missing,
 *          and ".png" is appended, so "File Info" and "sensorviz-file-info"
 *          both give sensorviz-file-info.png, matching the user guide's image
 *          names.
 *
 * @param[in] name  Capture name from --capture-screenshot or a suite list.
 *
 * @return  The file name, without a directory; an empty string if @p name is
 *          blank. Callers reject blank names before reaching this.
 */
QString captureFileName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty()) {
        return {};
    }
    if (name.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        return name;
    }

    name = name.toLower();
    name.replace(QLatin1Char('_'), QLatin1Char('-'));
    name.replace(QLatin1Char(' '), QLatin1Char('-'));
    if (!name.startsWith(QStringLiteral("sensorviz-"))) {
        name.prepend(QStringLiteral("sensorviz-"));
    }
    return name + QStringLiteral(".png");
}

/**
 * @brief   Keep processing GUI events for a fixed time so layout, plots and
 *          popup menus are painted before a screenshot is taken.
 *
 * @param[in] milliseconds  Minimum time to spend, in ms. Events are processed
 *                          at least once even for zero or negative values.
 *
 * @warning Re-enters the event loop, so queued slots and timers run during
 *          the wait, and sleeps the GUI thread in 10 ms
 *          steps. Only suitable for the non-interactive capture path.
 */
void processGuiEventsFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    } while (timer.elapsed() < milliseconds);
}

/**
 * @brief   Screen to grab a widget's pixels from.
 *
 * @param[in] widget  Window being captured; may be null or not yet shown.
 *
 * @return  The screen of the widget's native window if it has one, otherwise
 *          the primary screen. May be null on a platform with no screens, in
 *          which case callers fall back to QWidget::grab().
 */
QScreen *screenForWidget(QWidget *widget)
{
    if (widget && widget->windowHandle() && widget->windowHandle()->screen()) {
        return widget->windowHandle()->screen();
    }
    return QGuiApplication::primaryScreen();
}

} // namespace

void MainWindow::runDocumentationStartup()
{
    processGuiEventsFor(150);

    bool ok = true;
    const bool capture_requested = !options_.captureScreenshot.isEmpty()
        || !options_.captureSuite.isEmpty() || !options_.dumpStatePath.isEmpty();

    const QString requested_single = options_.captureScreenshot.trimmed().toLower();
    const QString requested_suite = options_.captureSuite.trimmed().toLower();
    if (requested_single == QStringLiteral("startup")
        || requested_single == QStringLiteral("sensorviz-startup")
        || requested_suite == QStringLiteral("startup")
        || requested_suite == QStringLiteral("all")) {
        ok = captureDocumentationScreenshot(QStringLiteral("startup")) && ok;
        if (requested_suite == QStringLiteral("startup")
            || requested_single == QStringLiteral("startup")
            || requested_single == QStringLiteral("sensorviz-startup")) {
            QCoreApplication::exit(ok ? 0 : 1);
            return;
        }
    }

    if (!options_.loadLogPath.isEmpty()) {
        QString error;
        if (!loadLogFromPathForDocumentation(options_.loadLogPath, error)) {
            reportDocumentationError(tr("Could not load %1: %2").arg(options_.loadLogPath, error));
            ok = false;
        }
    }

    if (ok && !options_.loadPreferencesPath.isEmpty()) {
        QString error;
        if (!loadPreferencesFromFile(options_.loadPreferencesPath, error)) {
            reportDocumentationError(
                tr("Could not load preferences %1: %2")
                    .arg(options_.loadPreferencesPath, error));
            ok = false;
        } else if (!log_.path.empty()) {
            applyPreferencesForCurrentTag();
            qInfo().noquote() << "Loaded sensorViz preferences from"
                              << options_.loadPreferencesPath;
        }
    }

    processGuiEventsFor(250);

    if (ok && !options_.captureScreenshot.isEmpty()) {
        ok = captureDocumentationScreenshot(options_.captureScreenshot) && ok;
    }
    if (ok && !options_.captureSuite.isEmpty()
        && requested_suite != QStringLiteral("startup")) {
        ok = captureDocumentationSuite(options_.captureSuite) && ok;
    }
    if (ok && !options_.dumpStatePath.isEmpty()) {
        QString error;
        if (!writeStateDump(options_.dumpStatePath, error)) {
            reportDocumentationError(error);
            ok = false;
        }
    }

    if (capture_requested) {
        QCoreApplication::exit(ok ? 0 : 1);
    }
}

bool MainWindow::captureDocumentationScreenshot(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized.isEmpty()) {
        reportDocumentationError(tr("No screenshot name was provided."));
        return false;
    }

    if (normalized == QStringLiteral("file-info")
        || normalized == QStringLiteral("sensorviz-file-info")
        || normalized == QStringLiteral("imutag-file-info")
        || normalized == QStringLiteral("compasstag-file-info")
        || normalized == QStringLiteral("bitprestag-file-info")) {
        tabs_->setCurrentIndex(1);
    } else {
        tabs_->setCurrentIndex(0);
    }

    if (normalized == QStringLiteral("compass-view")
        || normalized == QStringLiteral("compasstag-plot")
        || normalized == QStringLiteral("sensorviz-compass-view")) {
        if (!compass_samples_.isEmpty()) {
            updateCompassDisplay(compass_samples_.at(compass_samples_.size() / 2).epoch);
        }
    }

    const auto capture_window = [this, name]() {
        processGuiEventsFor(200);

        const QString dir_path =
            options_.screenshotDir.isEmpty() ? defaultScreenshotDir() : options_.screenshotDir;
        QDir dir(dir_path);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            reportDocumentationError(tr("Could not create screenshot directory %1.").arg(dir_path));
            return false;
        }

        const QString file_name = captureFileName(name);
        const QString path = dir.filePath(file_name);
        QScreen *screen = screenForWidget(this);
        QPixmap pixmap;
        if (screen) {
            const QRect rect = frameGeometry();
            pixmap = screen->grabWindow(
                0,
                rect.x(),
                rect.y(),
                rect.width(),
                rect.height());
        }
        if (pixmap.isNull()) {
            pixmap = grab();
        }
        if (pixmap.isNull() || !pixmap.save(path, "PNG")) {
            reportDocumentationError(tr("Could not write screenshot %1.").arg(path));
            return false;
        }
        qInfo().noquote() << "Wrote" << path;
        return true;
    };

    const auto capture_menu = [this, name](QMenu *menu, const QPoint &global_pos) {
        if (!menu) {
            reportDocumentationError(tr("Screenshot %1 has no menu to capture.").arg(name));
            return false;
        }

        const QString dir_path =
            options_.screenshotDir.isEmpty() ? defaultScreenshotDir() : options_.screenshotDir;
        QDir dir(dir_path);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            reportDocumentationError(tr("Could not create screenshot directory %1.").arg(dir_path));
            return false;
        }

        menu->popup(global_pos);
        processGuiEventsFor(200);

        const QRect rect = menu->geometry().adjusted(-4, -4, 4, 4);
        QScreen *screen = screenForWidget(this);
        QPixmap pixmap;
        if (screen && rect.isValid()) {
            pixmap = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height());
        }
        // Wayland lets no client read the screen, so the grab above comes back
        // null there; render the menu itself instead, as the window capture
        // falls back to grab().
        if (pixmap.isNull()) {
            pixmap = menu->grab();
        }
        menu->hide();
        processGuiEventsFor(50);

        const QString path = dir.filePath(captureFileName(name));
        if (pixmap.isNull() || !pixmap.save(path, "PNG")) {
            reportDocumentationError(tr("Could not write screenshot %1.").arg(path));
            return false;
        }
        qInfo().noquote() << "Wrote" << path;
        return true;
    };

    const QPoint menu_pos = menuBar()->mapToGlobal(QPoint(8, menuBar()->height()));
    if (normalized == QStringLiteral("file-menu")
        || normalized == QStringLiteral("sensorviz-file-menu")) {
        return capture_menu(file_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("preferences-menu")
        || normalized == QStringLiteral("sensorviz-preferences-menu")) {
        return capture_menu(preferences_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("view-menu")
        || normalized == QStringLiteral("sensorviz-view-menu")) {
        return capture_menu(view_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("ranges-menu")
        || normalized == QStringLiteral("sensorviz-ranges-menu")) {
        return capture_menu(range_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("configuration-menu")
        || normalized == QStringLiteral("sensorviz-configuration-menu")) {
        return capture_menu(configuration_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("help-menu")
        || normalized == QStringLiteral("sensorviz-help-menu")) {
        return capture_menu(help_menu_, menu_pos);
    }
    if (normalized == QStringLiteral("popup-menu")
        || normalized == QStringLiteral("context-menu")
        || normalized == QStringLiteral("sensorviz-popup-menu")) {
        QMenu *menu = createPlotContextMenu(this);
        const QPoint pos = plot_view_->mapToGlobal(plot_view_->rect().center());
        const bool ok = capture_menu(menu, pos);
        menu->deleteLater();
        return ok;
    }

    if (normalized != QStringLiteral("startup") && log_.path.empty()) {
        reportDocumentationError(
            tr("Screenshot %1 requires --load-log unless it is the startup view.").arg(name));
        return false;
    }

    return capture_window();
}

bool MainWindow::captureDocumentationSuite(const QString &suite)
{
    const QString normalized = suite.trimmed().toLower();
    QStringList captures;
    if (normalized == QStringLiteral("menus")) {
        captures << QStringLiteral("file-menu")
                 << QStringLiteral("preferences-menu")
                 << QStringLiteral("view-menu")
                 << QStringLiteral("ranges-menu")
                 << QStringLiteral("configuration-menu")
                 << QStringLiteral("help-menu")
                 << QStringLiteral("popup-menu");
    } else if (normalized == QStringLiteral("dialogs")) {
        reportDocumentationError(
            tr("The dialogs suite needs the dialog-builder refactor before it can run."));
        return false;
    } else if (normalized == QStringLiteral("imutag")) {
        captures << QStringLiteral("imutag-plot")
                 << QStringLiteral("imutag-file-info")
                 << QStringLiteral("view-menu")
                 << QStringLiteral("ranges-menu");
    } else if (normalized == QStringLiteral("compasstag")) {
        captures << QStringLiteral("compasstag-plot")
                 << QStringLiteral("compass-view")
                 << QStringLiteral("compasstag-file-info")
                 << QStringLiteral("configuration-menu")
                 << QStringLiteral("popup-menu");
    } else if (normalized == QStringLiteral("bitprestag")) {
        captures << QStringLiteral("bitprestag-plot")
                 << QStringLiteral("bitprestag-file-info")
                 << QStringLiteral("view-menu")
                 << QStringLiteral("ranges-menu")
                 << QStringLiteral("configuration-menu");
    } else if (normalized == QStringLiteral("all")) {
        captures << QStringLiteral("main-window")
                 << QStringLiteral("file-info")
                 << QStringLiteral("file-menu")
                 << QStringLiteral("preferences-menu")
                 << QStringLiteral("view-menu")
                 << QStringLiteral("ranges-menu")
                 << QStringLiteral("configuration-menu")
                 << QStringLiteral("help-menu")
                 << QStringLiteral("popup-menu");
    } else {
        reportDocumentationError(tr("Unknown screenshot suite %1.").arg(suite));
        return false;
    }

    bool ok = true;
    for (const QString &capture : captures) {
        ok = captureDocumentationScreenshot(capture) && ok;
    }
    return ok;
}

void MainWindow::reportDocumentationError(const QString &message)
{
    qCritical().noquote() << message;
    if (!options_.noUserPrompts
        && options_.captureScreenshot.isEmpty()
        && options_.captureSuite.isEmpty()) {
        QMessageBox::critical(this, tr("SensorViz Documentation Capture"), message);
    }
}
