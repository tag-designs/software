/**
 * @file    stream_meta.cpp
 * @brief   sensorViz's built-in stream display defaults, and StreamMeta for stored streams.
 */
#include "stream_meta.h"

namespace sensorviz {

// ---------------------------------------------------------------------------
// Stream Display Defaults
// ---------------------------------------------------------------------------
//
// Add new stored or derived stream ids here when they need a specific initial
// color, visibility state, axis side, or fixed y-axis range. These defaults are
// sensorViz display policy; saved preference files contain only user overrides
// from this table.

StreamDisplayDefaults defaultDisplayForStream(const std::string &stream_id)
{
    // These are viewer presentation choices, not part of the SQLite scientific
    // data contract. Keep them keyed by stable stream id so future viewers can
    // choose their own colors/ranges while tagcore logs remain tool-neutral.
    if (stream_id == "activity") {
        return {Rgb{30, 90, 180}, true, AxisSide::Left, {true, 0.0, 100.0}};
    }
    if (stream_id == "pressure") {
        return {Rgb{25, 130, 105}, false, AxisSide::Right, {}};
    }
    if (stream_id == "sensor_temperature") {
        return {Rgb{210, 95, 35}, false, AxisSide::Right, {}};
    }
    if (stream_id == "core_temperature") {
        return {Rgb{180, 55, 55}, false, AxisSide::Right, {true, 0.0, 50.0}};
    }
    if (stream_id == "voltage") {
        return {Rgb{60, 145, 75}, false, AxisSide::Right, {true, 0.0, 5.0}};
    }
    if (stream_id == "imu_pressure") {
        return {Rgb{25, 130, 105}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_ax") {
        return {Rgb{0, 114, 178}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_ay") {
        return {Rgb{230, 159, 0}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_az") {
        return {Rgb{0, 158, 115}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_gx") {
        return {Rgb{126, 87, 194}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_gy") {
        return {Rgb{213, 94, 0}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_gz") {
        return {Rgb{0, 150, 165}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_mx") {
        return {Rgb{86, 180, 233}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_my") {
        return {Rgb{204, 121, 167}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_mz") {
        return {Rgb{160, 130, 0}, false, AxisSide::Right, {}};
    }
    if (stream_id == "imu_accel_magnitude") {
        return {Rgb{85, 110, 210}, true, AxisSide::Left, {true, 0.0, 2000.0}};
    }
    if (stream_id == "imu_gyro_magnitude") {
        return {Rgb{190, 80, 55}, true, AxisSide::Left, {}};
    }
    if (stream_id == "imu_mag_magnitude") {
        return {Rgb{105, 130, 40}, true, AxisSide::Left, {}};
    }
    return {};
}

StreamMeta storedStreamMeta(const sensordata::StreamInfo &info)
{
    StreamMeta stream;
    stream.id = info.id;
    stream.label = info.label;
    stream.units = info.units;
    stream.timeDomain = info.domain;
    stream.dataId = info.handle;
    const StreamDisplayDefaults defaults = defaultDisplayForStream(info.id);
    stream.color = defaults.color;
    stream.defaultVisible = defaults.visible;
    stream.axisSide = defaults.axisSide;
    stream.axisRange = defaults.axisRange;
    return stream;
}

} // namespace sensorviz
