/**
 * @file    stream_meta.h
 * @brief   Qt-free description of one plottable sensorViz stream, and its display defaults.
 *
 * @details Part of `sensorviz_core`, which uses only standard C++ and the
 *          `sensordata` library so that a non-Qt front end can reuse the view
 *          state and plot policy. A StreamMeta holds no samples; its samples
 *          are the `sensordata` stream named by StreamMeta::dataId.
 *
 * @see     view_model.h, plot_spec.h, host/applications/sensorviz/README.md
 */
#ifndef SENSORVIZ_CORE_STREAM_META_H
#define SENSORVIZ_CORE_STREAM_META_H

#include <cstdint>
#include <string>

#include "sensordata.h"

namespace sensorviz {

/**
 * @struct  Rgb
 * @brief   An opaque 8-bit-per-channel color.
 */
struct Rgb
{
    std::uint8_t r = 0; ///< Red, 0-255.
    std::uint8_t g = 0; ///< Green, 0-255.
    std::uint8_t b = 0; ///< Blue, 0-255.

    /// Equal when every channel is equal.
    bool operator==(const Rgb &other) const
    {
        return r == other.r && g == other.g && b == other.b;
    }
    /// Not equal when any channel differs.
    bool operator!=(const Rgb &other) const { return !(*this == other); }
};

/**
 * @enum    AxisSide
 * @brief   Which side of the plot a stream's value axis occupies.
 */
enum class AxisSide
{
    Left,  ///< Left of the data area.
    Right, ///< Right of the data area.
};

/**
 * @struct  AxisRange
 * @brief   An optional fixed y range, used until the user sets one.
 */
struct AxisRange
{
    bool enabled = false; ///< False means the range comes from the data.
    double lower = 0.0;   ///< Lower bound, stream units.
    double upper = 0.0;   ///< Upper bound, stream units.
};

/**
 * @struct  StreamMeta
 * @brief   One plottable scalar stream: identity, labels, display defaults and its plot data.
 *
 * @details This is what the View menu, the plot and the transforms work with.
 *          New stored streams are described by the log's `streams` metadata;
 *          transforms add derived ones. Display fields are sensorViz's
 *          defaults; the user's overrides live in the ViewModel.
 */
struct StreamMeta
{
    std::string id;     ///< Stable id used by menus, transforms and saved preferences, e.g. `imu_ax`.
    std::string label;  ///< User-facing name.
    std::string units;  ///< Units; may be empty.
    sensordata::TimeDomain timeDomain = sensordata::TimeDomain::EpochSeconds; ///< Domain of the samples' times.
    sensordata::StreamId dataId = sensordata::kNoStream; ///< The samples in the log's sensordata::Log.
    std::string group;             ///< Axis group: the log's `streams.group_id` (e.g. `imu_accel`), or for a derived stream the group of its inputs; empty for a stream with its own axis.
    std::string groupLabel;        ///< User-facing group name (`streams.group_name`), used for a shared axis's label; may be empty.
    Rgb color{80, 100, 140};       ///< Default line and axis color.
    bool derived = false;          ///< Shown while it exists, without its own View entry, and drawn heavier (the activity filter).
    bool defaultVisible = true;    ///< Visibility before preferences are applied.
    AxisSide axisSide = AxisSide::Left; ///< Default axis side.
    AxisRange axisRange;           ///< Default fixed y range, if any.
};

/**
 * @struct  StreamDisplayDefaults
 * @brief   sensorViz's built-in display policy for one stream id.
 */
struct StreamDisplayDefaults
{
    Rgb color{80, 100, 140};            ///< Default color.
    bool visible = true;                ///< Visible on load.
    AxisSide axisSide = AxisSide::Left; ///< Default axis side.
    AxisRange axisRange;                ///< Default fixed range.
};

/**
 * @brief   Built-in display policy for a stable stream id.
 *
 * @details Viewer choices, not part of the SQLite data contract, keyed by
 *          stream id so a future tag that emits, say, `voltage` inherits the
 *          voltage policy. Unknown ids get a neutral color, visible, left axis
 *          and no fixed range.
 *
 * @param[in] streamId  Stream id.
 * @return  The defaults.
 */
StreamDisplayDefaults defaultDisplayForStream(const std::string &streamId);

/**
 * @brief   The StreamMeta for a stored stream of a log, with built-in defaults.
 * @param[in] info  The stream as the plot backend describes it.
 * @return  Metadata naming @p info's samples.
 */
StreamMeta storedStreamMeta(const sensordata::StreamInfo &info);

} // namespace sensorviz

#endif
