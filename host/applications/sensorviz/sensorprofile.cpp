#include "sensorprofile.h"

// sensorprofile.cpp is the declarative catalog of log tables sensorViz knows
// how to read. Add simple scalar tables here first; only move to loader/UI code
// when a table needs special parsing or multi-stream derivation.

namespace
{

// Common scalar table definitions shared by current SQLite tag logs. The loader
// treats every entry as optional, so it is safe for one catalog to cover PresTag,
// BitTag, and CompassTag subsets.
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

// Multi-column tables are loaded as SensorRecordSet and consumed later by
// transform code. Compass raw samples are the first concrete use.
QVector<RecordSetDefinition> commonRecordSets()
{
    return {
        {"compass_raw", "Compass raw", "Compass", {"ax", "ay", "az", "mx", "my", "mz"}},
    };
}

// Transform metadata documents the expected inputs and user parameters. The
// current implementation still wires actions manually, but this keeps the
// future generic transform UI shape visible in one place.
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

// Build the base profile before tag-type naming. If a future tag needs different
// defaults or tables, branch in sensorProfileForTag and adjust this copy.
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

// Select a display profile from Info.tagtype. A missing or unknown tag type
// still gets the common table catalog so experimental logs can load if their
// tables match known schemas.
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
