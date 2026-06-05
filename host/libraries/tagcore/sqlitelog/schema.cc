#include "sqlitelog/internal.h"

namespace tagcore::sqlite_log {

// Schema catalog for SQLite logs. Payload decoding lives in the tag-specific
// files; this file only describes tables and stream metadata for each tag type.
//
// The stream catalog is intentionally descriptive rather than prescriptive:
// it tells viewers what data exists, where to read it, and the physical units.
// Viewer policy such as colors, axes, default visibility, and plot grouping
// belongs in the application layer, not in the database schema.
//
// When adding or renaming table columns here, update the corresponding INSERT
// statement in the matching tag decoder. The writer does not infer INSERT
// layouts from these definitions.

namespace {

SqlTableDefinition voltageTable()
{
    return {
        "Voltage",
        {{"Epoch", "INTEGER"}, {"Voltage", "REAL"}},
        {{
            "voltage",
            nullptr,
            nullptr,
            "Voltage",
            "Epoch",
            "Voltage",
            "scalar",
            "Voltage",
            "V",
            "voltage",
            "Tag supply voltage.",
        }},
    };
}

SqlTableDefinition coreTemperatureTable()
{
    return {
        "CoreTemperature",
        {{"Epoch", "INTEGER"}, {"Temperature", "REAL"}},
        {{
            "core_temperature",
            nullptr,
            nullptr,
            "CoreTemperature",
            "Epoch",
            "Temperature",
            "scalar",
            "Core temperature",
            "C",
            "temperature",
            "Internal tag temperature. Pressure tags use the source field for another voltage measurement and do not write this stream.",
        }},
    };
}

SqlTableDefinition activityTable()
{
    return {
        "Activity",
        {{"Epoch", "INTEGER"}, {"Activity", "REAL"}},
        {{
            "activity",
            nullptr,
            nullptr,
            "Activity",
            "Epoch",
            "Activity",
            "scalar",
            "Activity",
            "%",
            "activity",
            "Percent activity over the sample interval.",
        }},
    };
}

SqlTableDefinition pressureTable()
{
    return {
        "Pressure",
        {{"Epoch", "INTEGER"}, {"Pressure", "REAL"}},
        {{
            "pressure",
            nullptr,
            nullptr,
            "Pressure",
            "Epoch",
            "Pressure",
            "scalar",
            "Pressure",
            "mbar",
            "pressure",
            "Absolute pressure from the pressure sensor.",
        }},
    };
}

SqlTableDefinition sensorTemperatureTable()
{
    return {
        "Temperature",
        {{"Epoch", "INTEGER"}, {"Temperature", "REAL"}},
        {{
            "sensor_temperature",
            nullptr,
            nullptr,
            "Temperature",
            "Epoch",
            "Temperature",
            "scalar",
            "Sensor temperature",
            "C",
            "temperature",
            "Temperature reported by the pressure sensor.",
        }},
    };
}

SqlTableDefinition compassTable()
{
    return {
        "Compass",
        {
            {"Epoch", "INTEGER"},
            {"ax", "REAL"},
            {"ay", "REAL"},
            {"az", "REAL"},
            {"mx", "REAL"},
            {"my", "REAL"},
            {"mz", "REAL"},
        },
        {
            {
                "compass_ax", "compass_raw", "Compass raw", "Compass", "Epoch", "ax",
                "record_column", "Acceleration X", "mg", "acceleration_x",
                "Raw accelerometer X sample used for compass orientation.",
            },
            {
                "compass_ay", "compass_raw", "Compass raw", "Compass", "Epoch", "ay",
                "record_column", "Acceleration Y", "mg", "acceleration_y",
                "Raw accelerometer Y sample used for compass orientation.",
            },
            {
                "compass_az", "compass_raw", "Compass raw", "Compass", "Epoch", "az",
                "record_column", "Acceleration Z", "mg", "acceleration_z",
                "Raw accelerometer Z sample used for compass orientation.",
            },
            {
                "compass_mx", "compass_raw", "Compass raw", "Compass", "Epoch", "mx",
                "record_column", "Magnetic field X", "uT", "magnetic_field_x",
                "Raw magnetometer X sample used for compass orientation.",
            },
            {
                "compass_my", "compass_raw", "Compass raw", "Compass", "Epoch", "my",
                "record_column", "Magnetic field Y", "uT", "magnetic_field_y",
                "Raw magnetometer Y sample used for compass orientation.",
            },
            {
                "compass_mz", "compass_raw", "Compass raw", "Compass", "Epoch", "mz",
                "record_column", "Magnetic field Z", "uT", "magnetic_field_z",
                "Raw magnetometer Z sample used for compass orientation.",
            },
        },
    };
}

SqlTableDefinition imuHeaderTable()
{
    /*
     * IMUTag has two time domains:
     * - ImuHeader stores the absolute wall-clock timestamp supplied by the tag
     *   at the start of each 16-block page.
     * - Sensor rows use elapsed microseconds from the first retained block.
     *
     * Sensorviz currently treats time columns generically, so keeping this
     * origin information explicit gives future viewers enough data to convert
     * elapsed time to absolute time if they need it.
     */
    return {
        "ImuHeader",
        {
            {"HeaderIndex", "INTEGER"},
            {"StartElapsedUs", "INTEGER"},
            {"Epoch", "INTEGER"},
            {"Millisecond", "INTEGER"},
            {"Temperature", "REAL"},
        },
        {},
    };
}

SqlTableDefinition imuPressureTable()
{
    return {
        "ImuPressure",
        {{"ElapsedUs", "INTEGER"}, {"Pressure", "REAL"}},
        {{
            "imu_pressure",
            nullptr,
            nullptr,
            "ImuPressure",
            "ElapsedUs",
            "Pressure",
            "scalar",
            "Pressure",
            "mbar",
            "pressure",
            "IMUTag pressure sample timestamped by elapsed collection time.",
        }},
    };
}

SqlTableDefinition imuMagTable()
{
    return {
        "ImuMag",
        {
            {"ElapsedUs", "INTEGER"},
            {"mx", "REAL"},
            {"my", "REAL"},
            {"mz", "REAL"},
        },
        {
            {
                "imu_mx", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "mx",
                "scalar", "Magnetic field X", "uT", "magnetic_field_x",
                "IMUTag magnetometer X sample timestamped by elapsed collection time.",
            },
            {
                "imu_my", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "my",
                "scalar", "Magnetic field Y", "uT", "magnetic_field_y",
                "IMUTag magnetometer Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_mz", "imu_mag", "IMU magnetometer", "ImuMag", "ElapsedUs", "mz",
                "scalar", "Magnetic field Z", "uT", "magnetic_field_z",
                "IMUTag magnetometer Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

SqlTableDefinition imuAccelTable()
{
    return {
        "ImuAccel",
        {
            {"ElapsedUs", "INTEGER"},
            {"ax", "REAL"},
            {"ay", "REAL"},
            {"az", "REAL"},
        },
        {
            {
                "imu_ax", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "ax",
                "scalar", "Acceleration X", "mg", "acceleration_x",
                "IMUTag accelerometer X sample timestamped by elapsed collection time.",
            },
            {
                "imu_ay", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "ay",
                "scalar", "Acceleration Y", "mg", "acceleration_y",
                "IMUTag accelerometer Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_az", "imu_accel", "IMU accelerometer", "ImuAccel", "ElapsedUs", "az",
                "scalar", "Acceleration Z", "mg", "acceleration_z",
                "IMUTag accelerometer Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

SqlTableDefinition imuGyroTable()
{
    return {
        "ImuGyro",
        {
            {"ElapsedUs", "INTEGER"},
            {"gx", "REAL"},
            {"gy", "REAL"},
            {"gz", "REAL"},
        },
        {
            {
                "imu_gx", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gx",
                "scalar", "Gyroscope X", "dps", "angular_velocity_x",
                "IMUTag gyroscope X sample timestamped by elapsed collection time.",
            },
            {
                "imu_gy", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gy",
                "scalar", "Gyroscope Y", "dps", "angular_velocity_y",
                "IMUTag gyroscope Y sample timestamped by elapsed collection time.",
            },
            {
                "imu_gz", "imu_gyro", "IMU gyroscope", "ImuGyro", "ElapsedUs", "gz",
                "scalar", "Gyroscope Z", "dps", "angular_velocity_z",
                "IMUTag gyroscope Z sample timestamped by elapsed collection time.",
            },
        },
    };
}

} // namespace

SqlTagProfile sqliteProfileForTag(TagType tag_type)
{
    switch (tag_type) {
    case BITTAG:
        return {false, {voltageTable(), coreTemperatureTable(), activityTable()}};
    case COMPASSTAG:
        return {
            true,
            {voltageTable(), coreTemperatureTable(), activityTable(), compassTable()}};
    case PRESTAG:
        return {false, {voltageTable(), pressureTable(), sensorTemperatureTable()}};
    case BITPRESTAG:
        return {false, {voltageTable(), activityTable(), pressureTable(), sensorTemperatureTable()}};
    case IMUTAG:
        return {
            false,
            {imuHeaderTable(), imuPressureTable(), imuMagTable(), imuAccelTable(), imuGyroTable()}};
    default:
        return {false, {}};
    }
}

} // namespace tagcore::sqlite_log
