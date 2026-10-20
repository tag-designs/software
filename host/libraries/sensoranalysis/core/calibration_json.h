/**
 * @file    calibration_json.h
 * @brief   Qt-free reader for the stored CalibrationConstants JSON.
 *
 * @details Parses the Constants column of a log's Calibration table with
 *          protobuf's generic JSON form (google::protobuf::Struct) and maps it
 *          through compasscore::calibrationFromLookup(). Struct holds every
 *          number as the double parsed from the text, exactly as Qt's JSON
 *          reader does, so the result is bit-identical to
 *          CompassCalibration::fromCalibrationJson(); parsing into the typed
 *          CalibrationConstants message would not be, because its fields are
 *          float.
 *
 *          Built as the sensoranalysis_core_json target, which links protobuf;
 *          sensoranalysis_core itself needs no JSON library.
 */
#ifndef SENSORANALYSIS_CORE_CALIBRATION_JSON_H
#define SENSORANALYSIS_CORE_CALIBRATION_JSON_H

#include "compass_core.h"

#include <string>

namespace compasscore
{

/**
 * @brief   Reads calibration constants from stored CalibrationConstants JSON.
 *
 * @param[in]  json      The JSON text, as stored in Calibration.Constants.
 * @param[out] constants The calibration; unchanged on failure.
 * @param[out] error     Why the text was refused; unchanged on success.
 *
 * @return  true on success; false when the text is not a JSON object or has
 *          no "magnetometer" object, since without one there is no
 *          magnetometer calibration to apply.
 */
bool calibrationFromJson(
    const std::string &json,
    CalibrationConstants &constants,
    std::string &error);

} // namespace compasscore

#endif // SENSORANALYSIS_CORE_CALIBRATION_JSON_H
