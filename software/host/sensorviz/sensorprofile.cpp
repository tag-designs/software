#include "sensorprofile.h"

namespace
{

QVector<ScalarStreamDefinition> commonScalarStreams()
{
    return {
        {
            "activity",
            "Activity",
            "%",
            "Activity",
            "Activity",
            QColor(30, 90, 180),
            true,
            SensorAxisSide::Left,
            {true, 0.0, 100.0},
        },
        {"pressure", "Pressure", "mbar", "Pressure", "Pressure", QColor(25, 130, 105), true},
        {"sensor_temperature", "Sensor temperature", "C", "Temperature", "Temperature", QColor(210, 95, 35), true},
        {
            "core_temperature",
            "Core temperature",
            "C",
            "CoreTemperature",
            "Temperature",
            QColor(180, 55, 55),
            false,
            SensorAxisSide::Right,
            {true, 0.0, 50.0},
        },
        {
            "voltage",
            "Voltage",
            "V",
            "Voltage",
            "Voltage",
            QColor(60, 145, 75),
            false,
            SensorAxisSide::Right,
            {true, 0.0, 5.0},
        },
    };
}

QVector<RecordSetDefinition> commonRecordSets()
{
    return {
        {"compass_raw", "Compass raw", "Compass", {"ax", "ay", "az", "mx", "my", "mz"}},
    };
}

QVector<SensorTransformDefinition> commonTransforms()
{
    return {
        {
            "altitude",
            "Altitude",
            "altitude",
            {"pressure"},
            {},
            {{"sea_level_pressure", "Sea-level pressure", "mbar", 1013.25, 800.0, 1200.0, 2}},
        },
        {
            "activity_lowpass",
            "Activity filter",
            "activity_lowpass",
            {"activity"},
            {},
            {{"time_constant", "Time constant", "seconds", 600.0, 1.0, 86400.0, 1}},
        },
    };
}

SensorProfile commonProfile()
{
    SensorProfile profile;
    profile.displayName = "Sensor log";
    profile.scalarStreams = commonScalarStreams();
    profile.recordSets = commonRecordSets();
    profile.transforms = commonTransforms();
    return profile;
}

} // namespace

SensorProfile sensorProfileForTag(const QString &tagType)
{
    SensorProfile profile = commonProfile();
    profile.tagType = tagType;

    if (tagType.compare("PRESTAG", Qt::CaseInsensitive) == 0) {
        profile.displayName = "PresTag";
    } else if (tagType.compare("BITTAG", Qt::CaseInsensitive) == 0) {
        profile.displayName = "BitTag";
    } else if (tagType.compare("COMPASSTAG", Qt::CaseInsensitive) == 0) {
        profile.displayName = "CompassTag";
    }

    return profile;
}
