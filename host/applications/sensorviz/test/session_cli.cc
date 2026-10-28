/**
 * @file    session_cli.cc
 * @brief   sensorViz without Qt: opens a log with sensorviz::Session and
 *          writes the state dump, or prints the hover readout.
 *
 * @details A third client of `sensorviz_core`, neither Qt nor a webview, so
 *          that the core's boundary is shown to be real: everything it prints
 *          comes from Session. Its dump is the Qt app's `--dump-state`, made
 *          through Session alone, and must equal the Qt app's for the same log
 *          up to the activity filter, which only the Qt app has.
 *
 *              session_cli <log.db3> [--load-preferences <file>] --dump-state <out>
 *              session_cli <log.db3> [--load-preferences <file>] --hover <seconds>
 *
 *          Exits 0 on success, 1 when the log or preference file cannot be
 *          read or the output cannot be written, 2 on bad arguments.
 */
#include "session.h"
#include "state_dump.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace
{

/**
 * @class   SessionDriver
 * @brief   Makes the state dump's changes through the Session alone.
 */
class SessionDriver : public sensorviz::StateDumpDriver
{
public:
    /// A driver over @p session.
    explicit SessionDriver(sensorviz::Session &session) : session_(session) {}

    /// Sets the sea-level pressure; altitude is remade.
    void setSeaLevelPressure(double mbar) override
    {
        setParameter(sensorviz::ViewModel::kSeaLevelPressure, mbar);
    }
    /// Sets a range and the ranges linked to it.
    void setStreamRange(const std::string &id, double lower, double upper) override
    {
        session_.setStreamRange(id, lower, upper);
    }
    /// Sets the declination; the heading is remade.
    void setDeclination(double degrees) override
    {
        setParameter(sensorviz::ViewModel::kDeclination, degrees);
    }
    /// Flips the battery direction; the heading is remade.
    void toggleBatteryForward() override
    {
        setParameter(sensorviz::ViewModel::kBatteryForward, session_.view().batteryForward() ? 0.0 : 1.0);
    }
    /// Joins lines across gaps.
    void joinGaps() override { session_.view().setBreakAtGaps(false); }
    /// Sets the UTC offset.
    void setUtcOffset(int hours) override { session_.view().setUtcOffsetHours(hours); }
    /// Clears the custom ranges.
    void resetZoom() override { session_.resetZoom(); }

private:
    /// Sets a transform parameter, reporting a failed remake.
    void setParameter(const char *id, double value)
    {
        std::string error;
        if (!session_.setParameter(id, value, error)) {
            std::fprintf(stderr, "%s: %s\n", id, error.c_str());
        }
    }

    sensorviz::Session &session_; ///< The session the changes are made in.
};

/// The whole of the file @p path, or false.
bool readFile(const std::string &path, std::string &text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

/// Prints the usage and returns the bad-arguments status.
int usage()
{
    std::fprintf(stderr,
                 "usage: session_cli <log.db3> [--load-preferences <file>] "
                 "(--dump-state <out> | --hover <seconds>)\n");
    return 2;
}

} // namespace

/// Opens the log and does what the arguments ask; see the file comment.
int main(int argc, char **argv)
{
    if (argc < 4) {
        return usage();
    }
    const std::string log = argv[1];
    std::string preferencesPath;
    std::string dumpPath;
    std::string hoverTime;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string option = argv[i];
        if (option == "--load-preferences") {
            preferencesPath = argv[i + 1];
        } else if (option == "--dump-state") {
            dumpPath = argv[i + 1];
        } else if (option == "--hover") {
            hoverTime = argv[i + 1];
        } else {
            return usage();
        }
    }
    if (dumpPath.empty() == hoverTime.empty()) {
        return usage();
    }

    sensorviz::PreferenceStore preferences;
    if (!preferencesPath.empty()) {
        std::string json;
        std::string error;
        if (!readFile(preferencesPath, json) || !preferences.load(json, preferencesPath, error)) {
            std::fprintf(stderr, "Could not load preferences %s: %s\n", preferencesPath.c_str(),
                         error.c_str());
            return 1;
        }
    }

    sensorviz::Session session;
    std::string error;
    if (!session.open(log, &preferences, error)) {
        std::fprintf(stderr, "Could not load %s: %s\n", log.c_str(), error.c_str());
        return 1;
    }
    for (const auto &[id, message] : session.transformFailures()) {
        std::fprintf(stderr, "%s: %s\n", id.c_str(), message.c_str());
    }

    if (!hoverTime.empty()) {
        for (const std::string &row : session.hover(std::strtod(hoverTime.c_str(), nullptr))) {
            std::printf("%s\n", row.c_str());
        }
        return 0;
    }

    SessionDriver driver(session);
    const std::string dump = sensorviz::runStateDump(session, driver);
    std::ofstream out(dumpPath, std::ios::binary);
    out << dump;
    if (!out) {
        std::fprintf(stderr, "Could not write %s\n", dumpPath.c_str());
        return 1;
    }
    return 0;
}
