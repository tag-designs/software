#ifndef COMPASS_PROCESSOR_H
#define COMPASS_PROCESSOR_H

#include "compass_types.h"

class CompassProcessor
{
public:
    explicit CompassProcessor(const CompassCalibration &calibration);

    /**
     * @brief   Converts one raw sample into magnetic-frame orientation data.
     *
     * @details Applies the processor calibration to the magnetometer vector,
     *          solves orientation from magnetometer and accelerometer vectors,
     *          and leaves heading display choices out of the returned sample.
     *          Applications layer declination and mounting convention onto yaw
     *          when they need a displayed heading.
     *
     * @param[in] sample Raw accelerometer and magnetometer sample.
     *
     * @return  Derived sample. If the input vectors are unusable, fields remain
     *          default-initialized.
     */
    CompassDerivedSample deriveSample(const CompassRawSample &sample) const;

    /**
     * @brief   Converts one raw sample into magnetic-frame orientation data.
     *
     * @param[in] sample Raw accelerometer and magnetometer sample.
     * @param[out] derived Populated with orientation data on success.
     *
     * @return  @c true when orientation could be solved, @c false when the
     *          input vectors are empty or otherwise unusable.
     */
    bool deriveSample(const CompassRawSample &sample, CompassDerivedSample &derived) const;

    /**
     * @brief   Derives orientation from an already-calibrated magnetometer.
     *
     * @details qtcalibrate applies live calibration and low-pass filtering
     *          before solving orientation. This path shares the same eCompass
     *          solve without applying the processor calibration a second time.
     *
     * @param[in] sample Accelerometer and already-calibrated magnetometer data.
     *
     * @return  Derived sample. If the input vectors are unusable, fields remain
     *          default-initialized.
     */
    CompassDerivedSample deriveCalibratedSample(const CompassRawSample &sample) const;

    /**
     * @brief   Derives orientation from an already-calibrated magnetometer.
     *
     * @param[in] sample Accelerometer and already-calibrated magnetometer data.
     * @param[out] derived Populated with orientation data on success.
     *
     * @return  @c true when orientation could be solved, @c false when the
     *          input vectors are empty or otherwise unusable.
     */
    bool deriveCalibratedSample(
        const CompassRawSample &sample,
        CompassDerivedSample &derived) const;

private:
    bool computeOrientation(
        QVector3D mag,
        QVector3D accel,
        QQuaternion &q,
        float &dip,
        float &field,
        float &mg,
        bool applyCalibration) const;

    bool deriveSample(
        const CompassRawSample &sample,
        bool applyCalibration,
        CompassDerivedSample &derived) const;

    CompassCalibration calibration_;
};

#endif // COMPASS_PROCESSOR_H
