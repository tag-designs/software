/**
 * @file    state_dump.cpp
 * @brief   sensorViz's maintainer-only state dump (--dump-state): the core's
 *          script (core/state_dump.h), driven through the Qt app.
 *
 * @details Implements MainWindow::writeStateDump(). The script and the text
 *          are the core's, shared with `session_cli`; this file supplies the
 *          driver, which makes each change through the same functions the
 *          Qt dialogs and menu actions call, so the dump checks the Qt glue
 *          as well as the core. Dumps from the two front ends of the same log
 *          are identical up to the activity filter, which only this one has.
 */

#include "mainwindow.h"

#include <QFile>
#include <QSignalBlocker>

#include "state_dump.h"

/**
 * @class   MainWindow::StateDumpDriver
 * @brief   Makes the state dump's changes as the Qt app's dialogs and actions do.
 */
class MainWindow::StateDumpDriver : public sensorviz::StateDumpDriver
{
public:
    /// A driver over @p window.
    explicit StateDumpDriver(MainWindow &window) : window_(window) {}

    /// As accepting the Sea-level Pressure dialog does.
    void setSeaLevelPressure(double mbar) override { window_.applySeaLevelPressure(mbar); }
    /// As accepting a range dialog does.
    void setStreamRange(const std::string &id, double lower, double upper) override
    {
        window_.applyStreamRange(toQString(id), lower, upper);
    }
    /// As accepting the Declination dialog does.
    void setDeclination(double degrees) override { window_.applyDeclination(degrees); }
    /// Toggles the Battery Forward action, as a click does.
    void toggleBatteryForward() override
    {
        window_.battery_forward_action_->setChecked(!window_.battery_forward_action_->isChecked());
    }
    /// Unchecks the Break Lines at Gaps action, as a click does.
    void joinGaps() override { window_.break_gaps_action_->setChecked(false); }
    /// As accepting the UTC Offset dialog does.
    void setUtcOffset(int hours) override { window_.applyUtcOffset(hours); }
    /// As View > Reset Zoom does.
    void resetZoom() override { window_.resetZoom(); }
    /// Checks the Activity Filter action and makes the filter, without its dialog.
    bool enableActivityFilter(double seconds) override
    {
        {
            // The action's own toggle would open the time-constant dialog.
            const QSignalBlocker blocker(window_.activity_filter_action_);
            window_.activity_filter_action_->setChecked(true);
        }
        window_.enableActivityFilter(seconds);
        return true;
    }

private:
    MainWindow &window_; ///< The window whose functions make the changes.
};

bool MainWindow::writeStateDump(const QString &path, QString &error)
{
    if (log_.path.empty()) {
        error = tr("No log is loaded.");
        return false;
    }

    StateDumpDriver driver(*this);
    const std::string dump = sensorviz::runStateDump(session_, driver);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QByteArray::fromStdString(dump));
    return file.error() == QFileDevice::NoError;
}
