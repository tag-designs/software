/**
 * @file    state_dump.h
 * @brief   The state dump: a Session taken through a fixed script of setting
 *          changes, with everything the plot and readout would show written
 *          as text after each step.
 *
 * @details Part of `sensorviz_core`. It is the before-and-after check for
 *          changes that move behaviour without changing what the default
 *          screenshots show. A front end supplies a StateDumpDriver, which
 *          makes each change the way that front end does -- the Qt app
 *          through the functions its dialogs and menu actions call,
 *          `session_cli` through the Session alone -- so two dumps of the same
 *          log, one from each, are identical exactly when the two front ends
 *          agree. The Qt app's `--dump-state` and `session_cli` write it.
 *
 *          Each step records:
 *
 *          - the PlotSpec, every field, numbers as hex floats;
 *          - every stream in the ViewModel: metadata, visibility, and a hash
 *            of every sample the plot backend holds;
 *          - the custom ranges;
 *          - hover rows (time text and every stream's value text) at 25
 *            evenly spaced times;
 *          - the compass orientation samples, hashed.
 *
 *          The format is for diff, not for reading back.
 */
#ifndef SENSORVIZ_CORE_STATE_DUMP_H
#define SENSORVIZ_CORE_STATE_DUMP_H

#include <string>

#include "session.h"

namespace sensorviz {

/**
 * @class   StateDumpDriver
 * @brief   Makes the script's changes the way one front end makes them.
 */
class StateDumpDriver
{
public:
    virtual ~StateDumpDriver() = default;

    /// Sets the sea-level pressure, mbar, as Configuration > Sea-level Pressure does.
    virtual void setSeaLevelPressure(double mbar) = 0;
    /// Sets a y range the user chose, as View > Ranges does.
    virtual void setStreamRange(const std::string &id, double lower, double upper) = 0;
    /// Sets the declination, degrees, as Configuration > Declination does.
    virtual void setDeclination(double degrees) = 0;
    /// Toggles Configuration > Battery Forward.
    virtual void toggleBatteryForward() = 0;
    /// Turns off View > Break Lines at Gaps.
    virtual void joinGaps() = 0;
    /// Sets the UTC offset, hours, as Configuration > UTC Offset does.
    virtual void setUtcOffset(int hours) = 0;
    /// View > Reset Zoom.
    virtual void resetZoom() = 0;
    /**
     * @brief   Turns on the activity filter with a time constant, seconds.
     * @return  false when the front end has no activity filter (only the Qt
     *          app has one), in which case the step is left out.
     */
    virtual bool enableActivityFilter(double seconds)
    {
        (void)seconds;
        return false;
    }
};

/**
 * @class   SessionDumpDriver
 * @brief   Makes the state dump's changes through a Session alone, as a front
 *          end without Qt does; it has no activity filter.
 */
class SessionDumpDriver : public StateDumpDriver
{
public:
    /// A driver over @p session.
    explicit SessionDumpDriver(Session &session) : session_(session) {}

    void setSeaLevelPressure(double mbar) override;
    void setStreamRange(const std::string &id, double lower, double upper) override;
    void setDeclination(double degrees) override;
    void toggleBatteryForward() override;
    void joinGaps() override;
    void setUtcOffset(int hours) override;
    void resetZoom() override;

private:
    /// Sets a transform parameter, reporting a failed remake on stderr.
    void setParameter(const char *id, double value);

    Session &session_; ///< The session the changes are made in.
};

/**
 * @brief   One step of the dump: everything @p session shows now.
 * @param[in] session  The session.
 * @param[in] step     The step's name, written as its heading.
 * @return  The step's text, newline-terminated.
 */
std::string stateDumpStep(const Session &session, const std::string &step);

/**
 * @brief   Runs the script on @p session through @p driver, and the dump.
 *
 * @details After the load: sea-level pressure 1000 mbar; a pressure range of
 *          950-1010 mbar, which links the altitude range (when there is
 *          pressure); declination 12.5 degrees; battery forward toggled; gaps
 *          joined; UTC -5 (epoch-time logs only); Reset Zoom; and last the
 *          activity filter at 600 s, when the log has activity and the front
 *          end has the filter. A step is dumped after each.
 *
 * @param[in,out] session  The session, with a log loaded.
 * @param[in,out] driver   Makes the changes.
 * @return  The dump.
 */
std::string runStateDump(const Session &session, StateDumpDriver &driver);

} // namespace sensorviz

#endif
