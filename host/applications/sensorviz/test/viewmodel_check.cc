/**
 * @file    viewmodel_check.cc
 * @brief   Offline assertion checks for sensorviz::ViewModel, without Qt.
 *
 * @details Drives the ViewModel the way sensorViz's menus and dialogs do and
 *          checks its state, its change flags and listener, stream ordering,
 *          ranges, colors and sides, the per-tag preference rules (including
 *          the comparison earlier versions used, so saved files stay
 *          identical) and the PlotState and PlotSpec it builds. Streams are
 *          synthetic and carry no plot data.
 *
 *          Exits 0 when every check passes, 1 otherwise; each failure is
 *          printed with its line.
 */
#include "view_model.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

using sensorviz::AxisSide;
using sensorviz::PlotRange;
using sensorviz::Rgb;
using sensorviz::StreamMeta;
using sensorviz::TagPreferences;
using sensorviz::ViewModel;

int g_failures = 0;
int g_checks = 0;

/// Records one check; prints the failing expression and line.
#define CHECK(condition)                                                              \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(condition)) {                                                           \
            g_failures++;                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                             \
    } while (0)

/// A stored stream with sensorViz's defaults for @p id and a fake data handle.
StreamMeta stored(const std::string &id, sensordata::StreamId data)
{
    sensordata::StreamInfo info;
    info.id = id;
    info.label = id + " label";
    info.units = "u";
    info.handle = data;
    return sensorviz::storedStreamMeta(info);
}

/// An IMUTag-like stream list: pressure (hidden by default), three accel axes (hidden), activity (shown).
std::vector<StreamMeta> imuStreams()
{
    return {stored("imu_pressure", 1), stored("imu_ax", 2), stored("imu_ay", 3),
            stored("imu_az", 4), stored("activity", 5)};
}

void checkLoadAndOrder()
{
    ViewModel view;
    ViewModel::Changes heard = ViewModel::None;
    int calls = 0;
    view.setListener([&](ViewModel::Changes changes) {
        heard |= changes;
        calls++;
    });

    CHECK(!view.titleVisible());
    const ViewModel::Changes reset = view.resetForLog(imuStreams(), "log.db3");
    CHECK((reset & ViewModel::Streams) != 0 && calls == 1 && heard == reset);
    CHECK(view.title() == "log.db3" && view.titleVisible());
    CHECK((view.toggleableStreamIds()
           == std::vector<std::string>{"imu_pressure", "imu_ax", "imu_ay", "imu_az", "activity"}));
    // Defaults: IMU streams hidden, activity shown.
    CHECK((view.visibleStreamIds() == std::vector<std::string>{"activity"}));

    // A magnitude goes after the last of its axes; altitude after pressure.
    StreamMeta magnitude = stored("imu_accel_magnitude", 10);
    const ViewModel::StreamUpdate added = view.addOrReplaceStream(magnitude, true);
    CHECK(!added.replaced && added.released == sensordata::kNoStream);
    CHECK((added.changes & ViewModel::Visibility) != 0);
    StreamMeta altitude = stored("altitude", 11);
    view.addOrReplaceStream(altitude, true);
    CHECK((view.toggleableStreamIds()
           == std::vector<std::string>{"imu_pressure", "altitude", "imu_ax", "imu_ay", "imu_az",
                                       "imu_accel_magnitude", "activity"}));

    // A derived stream is shown while it exists, after the others.
    StreamMeta filter = stored("activity_lowpass", 12);
    filter.derived = true;
    const ViewModel::StreamUpdate filter_added = view.addOrReplaceStream(filter, false);
    CHECK((filter_added.changes & ViewModel::Visibility) != 0);
    CHECK(view.isVisible("activity_lowpass"));
    CHECK((view.visibleStreamIds()
           == std::vector<std::string>{"altitude", "imu_accel_magnitude", "activity",
                                       "activity_lowpass"}));
    CHECK(view.setStreamVisible("activity_lowpass", false) == ViewModel::None);

    // Replacing keeps the position, reports the old data, and flags a
    // visibility change only when there is one.
    magnitude.dataId = 20;
    const ViewModel::StreamUpdate same = view.addOrReplaceStream(magnitude, true);
    CHECK(same.replaced && same.released == 10);
    CHECK((same.changes & ViewModel::Visibility) == 0);
    magnitude.dataId = 21;
    const ViewModel::StreamUpdate hidden = view.addOrReplaceStream(magnitude, false);
    CHECK(hidden.replaced && (hidden.changes & ViewModel::Visibility) != 0);
    CHECK(!view.isVisible("imu_accel_magnitude"));
    filter.dataId = 22;
    const ViewModel::StreamUpdate filter_again = view.addOrReplaceStream(filter, true);
    CHECK(filter_again.replaced && filter_again.released == 12);
    CHECK((filter_again.changes & ViewModel::Visibility) == 0);
    CHECK(view.toggleableStreamIds()[5] == "imu_accel_magnitude");

    // Removing returns the data and forgets the stream's range.
    view.setCustomRange("activity_lowpass", PlotRange{1.0, 2.0}, true);
    CHECK(view.removeStream("activity_lowpass") == 22);
    CHECK(!view.hasCustomRange("activity_lowpass") && !view.isExplicitRange("activity_lowpass"));
    CHECK(view.removeStream("activity_lowpass") == sensordata::kNoStream);

    // Setters report nothing when nothing changes.
    calls = 0;
    CHECK(view.setStreamVisible("imu_ax", true) == ViewModel::Visibility);
    CHECK(view.setStreamVisible("imu_ax", true) == ViewModel::None);
    CHECK(view.setStreamVisible("no_such_stream", true) == ViewModel::None);
    CHECK(calls == 1);
    CHECK(view.setVisibleStreams({"imu_ax", "no_such_stream"}) == ViewModel::Visibility);
    CHECK((view.visibleStreamIds() == std::vector<std::string>{"imu_ax"}));
}

void checkRangesAndAppearance()
{
    ViewModel view;
    view.resetForLog(imuStreams(), "t");
    CHECK(view.setCustomRange("imu_pressure", PlotRange{1000.0, 1020.0}, true) == ViewModel::Ranges);
    CHECK(view.isExplicitRange("imu_pressure"));
    CHECK(view.setCustomRange("altitude", PlotRange{0.0, 100.0}, false) == ViewModel::Ranges);
    CHECK(!view.isExplicitRange("altitude") && view.hasCustomRange("altitude"));
    CHECK(view.setCustomRange("altitude", PlotRange{0.0, 100.0}, false) == ViewModel::None);
    CHECK(view.setCustomRange("altitude", PlotRange{0.0, 100.0}, true) == ViewModel::Ranges);
    CHECK(view.isExplicitRange("altitude"));
    CHECK(view.clearCustomRanges() == ViewModel::Ranges);
    CHECK(view.customRanges().empty() && !view.isExplicitRange("imu_pressure"));
    CHECK(view.clearCustomRanges() == ViewModel::None);

    // Colors and sides fall back to the stream's defaults.
    CHECK(view.effectiveColor("imu_ax") == (Rgb{0, 114, 178}));
    CHECK(view.effectiveAxisSide("imu_ax") == AxisSide::Right);
    CHECK(view.setColorOverrides({{"imu_ax", Rgb{1, 2, 3}}}) == ViewModel::Appearance);
    CHECK(view.setAxisSideOverrides({{"imu_ax", AxisSide::Left}}) == ViewModel::Appearance);
    CHECK(view.effectiveColor("imu_ax") == (Rgb{1, 2, 3}));
    CHECK(view.effectiveAxisSide("imu_ax") == AxisSide::Left);
    CHECK(view.setColorOverrides({{"imu_ax", Rgb{1, 2, 3}}}) == ViewModel::None);

    // View settings and transform parameters.
    CHECK(view.setTitleVisible(false) == ViewModel::Title);
    CHECK(view.setUtcOffsetHours(-5) == ViewModel::UtcOffset);
    CHECK(view.setCursorsVisible(false) == ViewModel::Cursors);
    CHECK(view.setBreakAtGaps(false) == ViewModel::Gaps);
    CHECK(view.setSeaLevelPressure(1000.5) == ViewModel::Parameters);
    CHECK(view.setDeclinationDegrees(12.5) == ViewModel::Parameters);
    CHECK(view.setBatteryForward(false) == ViewModel::Parameters);
    CHECK(view.setActivityTimeConstant(120.0) == ViewModel::Parameters);
    CHECK(view.setActivityTimeConstant(120.0) == ViewModel::None);

    // A new log keeps the session settings and drops per-log choices.
    view.setCustomRange("imu_ax", PlotRange{-1.0, 1.0}, true);
    view.resetForLog(imuStreams(), "second");
    CHECK(view.customRanges().empty() && view.colorOverrides().empty()
          && view.axisSideOverrides().empty());
    CHECK(view.titleVisible() && view.title() == "second");
    CHECK(view.utcOffsetHours() == -5 && !view.cursorsVisible() && !view.breakAtGaps());
    CHECK(view.seaLevelPressure() == 1000.5 && view.declinationDegrees() == 12.5);
    CHECK(!view.batteryForward() && view.activityTimeConstant() == 120.0);
}

void checkPreferences()
{
    ViewModel view;
    view.resetForLog(imuStreams(), "t");

    // Defaults capture as no overrides.
    TagPreferences none = view.capturePreferences("IMUTAG");
    CHECK(none.tagType == "IMUTAG" && !none.hasOverrides());

    // A color equal to the default is not stored; a different one is.
    view.setColorOverrides({{"imu_ax", Rgb{0, 114, 178}}, {"imu_ay", Rgb{9, 9, 9}}});
    view.setAxisSideOverrides({{"imu_ax", AxisSide::Right}, {"imu_ay", AxisSide::Left}});
    view.setStreamVisible("imu_ax", true);
    view.setBreakAtGaps(false);
    TagPreferences captured = view.capturePreferences("IMUTAG");
    CHECK(captured.hasVisibleStreamOverride);
    CHECK((captured.visibleStreamIds == std::set<std::string>{"activity", "imu_ax"}));
    CHECK(captured.streamColors.size() == 1 && captured.streamColors.count("imu_ay") == 1);
    CHECK(captured.axisSides.size() == 1 && captured.axisSides.at("imu_ay") == AxisSide::Left);
    CHECK(captured.hasBreakLinesAtGapsOverride && !captured.breakLinesAtGaps);

    // The visibility comparison counts derived streams' defaults too, as
    // earlier versions did: with the activity filter present the default set
    // includes it, so the shown set always differs and is stored.
    ViewModel derived_view;
    derived_view.resetForLog(imuStreams(), "t");
    StreamMeta filter = stored("activity_lowpass", 9);
    filter.derived = true;
    derived_view.addOrReplaceStream(filter, true);
    const TagPreferences with_filter = derived_view.capturePreferences("IMUTAG");
    CHECK(with_filter.hasVisibleStreamOverride);
    CHECK((with_filter.visibleStreamIds == std::set<std::string>{"activity"}));

    // Applying: unknown ids are dropped, missing overrides mean defaults.
    ViewModel fresh;
    fresh.resetForLog(imuStreams(), "t");
    TagPreferences file;
    file.hasVisibleStreamOverride = true;
    file.visibleStreamIds = {"imu_az", "no_such_stream"};
    file.streamColors = {{"imu_az", Rgb{5, 6, 7}}, {"no_such_stream", Rgb{1, 1, 1}}};
    file.axisSides = {{"no_such_stream", AxisSide::Left}};
    file.hasBreakLinesAtGapsOverride = true;
    file.breakLinesAtGaps = false;
    int calls = 0;
    ViewModel::Changes heard = ViewModel::None;
    fresh.setListener([&](ViewModel::Changes changes) {
        calls++;
        heard = changes;
    });
    const ViewModel::Changes applied = fresh.applyPreferences(file);
    CHECK(calls == 1 && heard == applied);
    CHECK((applied & ViewModel::Visibility) != 0 && (applied & ViewModel::Appearance) != 0
          && (applied & ViewModel::Gaps) != 0);
    CHECK((fresh.visibleStreamIds() == std::vector<std::string>{"imu_az"}));
    CHECK(fresh.colorOverrides().size() == 1 && fresh.axisSideOverrides().empty());
    CHECK(!fresh.breakAtGaps());

    // Round trip: what is applied is what is captured.
    TagPreferences round = fresh.capturePreferences("IMUTAG");
    CHECK(round.hasVisibleStreamOverride
          && (round.visibleStreamIds == std::set<std::string>{"imu_az"}));
    CHECK(round.streamColors.size() == 1 && round.streamColors.at("imu_az") == (Rgb{5, 6, 7}));
    CHECK(round.hasBreakLinesAtGapsOverride && !round.breakLinesAtGaps);

    // Empty preferences restore the defaults.
    fresh.applyPreferences(TagPreferences{});
    CHECK((fresh.visibleStreamIds() == std::vector<std::string>{"activity"}));
    CHECK(fresh.colorOverrides().empty() && fresh.breakAtGaps());
    CHECK(fresh.applyPreferences(TagPreferences{}) == ViewModel::None);
}

void checkPlotState()
{
    ViewModel view;
    view.resetForLog(imuStreams(), "plot");
    view.setVisibleStreams({"imu_ax", "imu_pressure"});
    view.setCustomRange("imu_pressure", PlotRange{1.0, 2.0}, true);
    view.setColorOverrides({{"imu_ax", Rgb{1, 2, 3}}});
    view.setUtcOffsetHours(3);
    view.setSeaLevelPressure(1001.0);
    view.setBreakAtGaps(false);

    const sensorviz::PlotState state = view.plotState();
    CHECK((state.visibleStreamIds == std::vector<std::string>{"imu_pressure", "imu_ax"}));
    CHECK(state.customRanges.at("imu_pressure") == (PlotRange{1.0, 2.0}));
    CHECK(state.utcOffsetHours == 3 && state.seaLevelPressure == 1001.0 && !state.breakAtGaps);
    CHECK(state.title == "plot" && state.titleVisible);

    const sensorviz::PlotSpec spec = view.plotSpec(sensorviz::PlotLogInfo{});
    CHECK(spec.traces.size() == 2 && spec.traces[0].id == "imu_pressure");
    CHECK(spec.axes[0].mode == sensorviz::PlotAxisRangeMode::Fixed && spec.axes[0].upper == 2.0);
    CHECK(spec.traces[1].color == (Rgb{1, 2, 3}));
    CHECK(spec.title == "plot" && !spec.breakAtGaps && spec.utcOffsetHours == 3);
    view.setTitleVisible(false);
    CHECK(view.plotSpec(sensorviz::PlotLogInfo{}).title.empty());
}

} // namespace

/**
 * @brief   Runs every check.
 * @return  0 when all pass, 1 otherwise.
 */
int main()
{
    checkLoadAndOrder();
    checkRangesAndAppearance();
    checkPreferences();
    checkPlotState();
    std::printf("viewmodel_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
