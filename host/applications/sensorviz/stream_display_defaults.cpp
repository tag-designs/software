// Built-in stream display defaults, declared in sensor_preferences.h. Kept
// apart from the preference code, which is MainWindow's, so the SQLite loader
// and buildPlotSpec() can be built and checked without a window.

#include "sensor_preferences.h"

// ---------------------------------------------------------------------------
// Stream Display Defaults
// ---------------------------------------------------------------------------
//
// Add new stored or derived stream ids here when they need a specific initial
// color, visibility state, axis side, or fixed y-axis range. These defaults are
// sensorViz display policy; saved preference files contain only user overrides
// from this table.

SensorStreamDisplayDefaults defaultDisplayForStream(const QString &stream_id)
{
    // These are viewer presentation choices, not part of the SQLite scientific
    // data contract. Keep them keyed by stable stream id so future viewers can
    // choose their own colors/ranges while tagcore logs remain tool-neutral.
    if (stream_id == "activity") {
        return {QColor(30, 90, 180), true, SensorAxisSide::Left, {true, 0.0, 100.0}};
    }
    if (stream_id == "pressure") {
        return {QColor(25, 130, 105), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "sensor_temperature") {
        return {QColor(210, 95, 35), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "core_temperature") {
        return {QColor(180, 55, 55), false, SensorAxisSide::Right, {true, 0.0, 50.0}};
    }
    if (stream_id == "voltage") {
        return {QColor(60, 145, 75), false, SensorAxisSide::Right, {true, 0.0, 5.0}};
    }
    if (stream_id == "imu_pressure") {
        return {QColor(25, 130, 105), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_ax") {
        return {QColor(0, 114, 178), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_ay") {
        return {QColor(230, 159, 0), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_az") {
        return {QColor(0, 158, 115), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_gx") {
        return {QColor(126, 87, 194), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_gy") {
        return {QColor(213, 94, 0), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_gz") {
        return {QColor(0, 150, 165), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_mx") {
        return {QColor(86, 180, 233), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_my") {
        return {QColor(204, 121, 167), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_mz") {
        return {QColor(160, 130, 0), false, SensorAxisSide::Right, {}};
    }
    if (stream_id == "imu_accel_magnitude") {
        return {QColor(85, 110, 210), true, SensorAxisSide::Left, {true, 0.0, 2000.0}};
    }
    if (stream_id == "imu_gyro_magnitude") {
        return {QColor(190, 80, 55), true, SensorAxisSide::Left, {}};
    }
    if (stream_id == "imu_mag_magnitude") {
        return {QColor(105, 130, 40), true, SensorAxisSide::Left, {}};
    }
    return {};
}
