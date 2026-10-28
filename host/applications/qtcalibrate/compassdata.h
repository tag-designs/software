#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QList>
#include <QVector3D>
#include <QQuaternion>
#include <QObject>

#include "accelcalibration.h"
#include "calibration_session.h"
#include "magquality.h"

/**
 * @class   CompassData
 * @brief   qtcalibrate's live calibration, on Qt types.
 *
 * @details An adapter over calibcore::CalibrationSession
 *          (sensoranalysis/core/calibration_session.h), which owns the sample
 *          buffer, Freescale's solver, retention, the accelerometer fit, the
 *          quality figures and the live compass filter. This class converts
 *          types and turns an accepted fit into the calibration_update()
 *          signal, so the Rust Tag Calibrator, which uses the session
 *          directly, calibrates exactly as qtcalibrate does.
 */
class CompassData : public QObject
{
    Q_OBJECT


public:

    /**
     * @brief   An empty calibration.
     * @param[in] parent  QObject parent.
     */
    explicit CompassData(QObject *parent = nullptr);

    /**
     * @brief   Feeds one calibration sample to the session.
     *
     * @param[in,out] mag  Raw magnetometer vector, µT; replaced with the
     *                     calibrated value when a calibration exists, for
     *                     plotting.
     * @param[in] hasAccel Whether this sample carried an accelerometer reading.
     * @param[in] accel    The accelerometer vector, mg.
     *
     * @return  true when a calibration exists after this sample (and @p mag
     *          was calibrated).
     *
     * @post    Emits calibration_update() when the solver accepted a new fit.
     */
    bool addData(QVector3D &mag, bool hasAccel = false,
                 const QVector3D &accel = QVector3D());

    /**
     * @brief   Appends every buffered reading, calibrated, to @p data.
     * @param[in,out] data  Receives the points, in slot order.
     */
    void getData(QList<QVector3D> &data);

    /**
     * @brief   The current magnetometer constants.
     * @param[out] B  Field magnitude, µT.
     * @param[out] V  Hard-iron offset, µT.
     * @param[out] A  Inverse soft-iron matrix.
     * @return  Whether a calibration exists.
     */
    bool getCalibrationConstants(float *B, float *V, float (*A)[3]);

    /**
     * @brief   Installs magnetometer constants read back from a tag.
     *
     * @details The tag does not store the field magnitude B. It is recomputed
     *          as the mean calibrated magnitude of the buffered samples, and
     *          left as it was when the buffer is empty.
     *
     * @param[in] V  Hard-iron offset, µT.
     * @param[in] A  Inverse soft-iron matrix.
     */
    void setCalibrationConstants(float *V, float(*A)[3]);

    /**
     * @brief   Metrics from the quality module, as of the last
     *          qualityUpdate().
     */
    const MagQuality::Result &qualityMetrics() const { return session.quality(); }

    /**
     * @brief   Fitted or loaded accelerometer zero-g offset, as of the last
     *          qualityUpdate() or setAccelOffset().
     *
     * @details Applied when deriving dip and orientation, recorded in a
     *          capture, and written to the tag with the magnetometer constants.
     */
    const AccelCalibration::Result &accelOffset() const { return gravity; }

    /**
     * @brief   Installs an accelerometer offset read back from a tag, in mg.
     *
     * @details Marks the result valid with no fit statistics. It stands until
     *          the live population yields a valid fit, or the buffer is
     *          cleared.
     *
     * @param[in] offset  Zero-g offset, mg.
     */
    void setAccelOffset(const QVector3D &offset);

    /// Evictions so far, and how many of them each rule decided.
    /// All reset with the buffer.
    int evictionCount() const { return session.evictionCount(); }
    /// Evictions decided as the least informative sample.
    int leverageEvictionCount() const { return session.leverageEvictionCount(); }
    /// Evictions decided as outliers.
    int outlierEvictionCount() const { return session.outlierEvictionCount(); }

    /// Recomputes the quality figures and the accelerometer fit.
    void qualityUpdate();

    /// Discards every sample and the fit.
    void clear();

    /**
     * @brief   Solves the live compass from one sample, after the session's
     *          low-pass filter.
     *
     * @param[in]  mag    Raw magnetometer vector, µT.
     * @param[in]  accel  Raw accelerometer vector, mg.
     * @param[out] q      Magnetic-frame orientation.
     * @param[out] dip    Dip angle, degrees.
     * @param[out] field  Field magnitude, µT.
     *
     * @return  false when no orientation exists; the outputs are then
     *          unchanged.
     */
    bool eCompass(QVector3D mag, QVector3D accel, QQuaternion &q,
                  float& dip, float& field);

    /// The field magnitude B, µT.
    float getField();

signals:

    /// The solver accepted a new fit.
    void calibration_update(void);

private:

    /// Copies the session's accelerometer result into gravity.
    void refreshGravity();

    calibcore::CalibrationSession session; ///< The calibration.

    /// The session's accelerometer result on Qt types, refreshed whenever
    /// the session can change it.
    AccelCalibration::Result gravity;
};

#endif
