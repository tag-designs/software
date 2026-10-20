/**
 * @file    calibration_json.cpp
 * @brief   Qt-free reader for the stored CalibrationConstants JSON.
 */
#include "calibration_json.h"

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

namespace compasscore
{

bool calibrationFromJson(
    const std::string &json,
    CalibrationConstants &constants,
    std::string &error)
{
    google::protobuf::Struct root;
    const auto status = google::protobuf::util::JsonStringToMessage(json, &root);
    if (!status.ok()) {
        error = "calibration constants are not a valid JSON object: "
            + std::string(status.message());
        return false;
    }

    const auto magnetometer = root.fields().find("magnetometer");
    if (magnetometer == root.fields().end() || !magnetometer->second.has_struct_value()) {
        error = "calibration constants do not contain a magnetometer object";
        return false;
    }

    // Same rule as QJsonValue::toDouble(fallback): a missing section, a
    // missing key or a value that is not a number all read as the fallback.
    const CalibrationLookup lookup = [&root](const char *section, const char *key, double fallback) {
        const auto s = root.fields().find(section);
        if (s == root.fields().end() || !s->second.has_struct_value()) {
            return fallback;
        }
        const auto &fields = s->second.struct_value().fields();
        const auto k = fields.find(key);
        if (k == fields.end() || k->second.kind_case() != google::protobuf::Value::kNumberValue) {
            return fallback;
        }
        // Qt's JSON reader returns +0 for "-0" and "-0.0"; protobuf keeps
        // the sign. Fold it, so a negative zero cannot flip the sign of an
        // all-zero product and the result stays bit-identical to Qt's reader.
        const double value = k->second.number_value();
        return value == 0.0 ? 0.0 : value;
    };

    constants = calibrationFromLookup(lookup);
    return true;
}

} // namespace compasscore
