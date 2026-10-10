#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QList>
#include <QVector3D>
#include <QQuaternion>
#include <QObject>

#include "magcal/magcal.h"
#include "gravityfit.h"
#include "magquality.h"
#include "magretention.h"

// CompassData is the adapter between qtcalibrate and the inherited C magcal
// solver. It owns the live calibration buffer and exposes a Qt-friendly API for
// MainWindow and magPlot. Orientation math itself lives in sensoranalysis.
class CompassData : public QObject
{
    Q_OBJECT


public:

    explicit CompassData(QObject *parent = nullptr);

    /**
     * @brief Feed one calibration sample to the solver.
     *
     * @param mag       Raw magnetometer vector; replaced with the calibrated
     *                  value when a calibration exists, for plotting.
     * @param hasAccel  Whether this sample carried an accelerometer reading.
     * @param accel     The accelerometer vector, in calibration stream units.
     *
     * @details The accelerometer takes no part in the fit. It is kept beside
     *          each buffered sample so the quality metrics can use it; see
     *          qualityMetrics().
     */
    bool addData(QVector3D &mag, bool hasAccel = false,
                 const QVector3D &accel = QVector3D());
    void getData(QList<QVector3D> &data);
    bool getCalibrationConstants(float *B, float *V, float (*A)[3]);
    void setCalibrationConstants(float B, float *V, float(*A)[3]);
    void calibrationQuality(float& gaps,float& variance, float& wobble, float& fiterror);

    /**
     * @brief Metrics from the owned quality module, as of the last
     *        qualityUpdate().
     *
     * @details Computed alongside calibrationQuality()'s inherited numbers
     *          while the two are compared; neither drives the other.
     */
    const MagQuality::Result &qualityMetrics() const { return metrics; }

    /**
     * @brief Fitted accelerometer zero-g offset, as of the last qualityUpdate().
     *
     * @details Host-side only: it is applied when deriving dip and orientation
     *          and recorded in a capture, but is not written to the tag. See
     *          host/docs/design/proposals/qtcalibrate-quality-replacement.md.
     */
    const GravityFit::Result &accelOffset() const { return gravity; }

    /// Which policy decides what to discard when the buffer is full.
    enum class Retention {
        Inherited,  ///< choose_discard_magcal() as it came from MotionCal.
        Leverage,   ///< MagRetention: lowest leverage, or worst Cook's distance.
    };

    void setRetention(Retention policy) { retentionPolicy = policy; }
    Retention retention() const { return retentionPolicy; }

    /// Evictions so far, and how many of them the leverage policy decided.
    /// Both reset with the buffer.
    int evictionCount() const { return evictions; }
    int leverageEvictionCount() const { return leverageEvictions; }
    int outlierEvictionCount() const { return outlierEvictions; }

    void qualityUpdate();
    void clear();
 
    //void getRegionData(QScatterDataArray& data, float magnitude);

    bool eCompass(QVector3D mag, QVector3D accel, QQuaternion &q, 
                  float& dip, float& field);
    float getField();

signals:

    void calibration_update(void);

private:

    void apply_calibration(QVector3D &mag);
    //void apply_calibration(float rawx, float rawy, float rawz, Point_t *out); 
    void raw_data_reset();
    int choose_discard_magcal(void);
    void add_magcal_data(const QVector3D &mag, bool hasAccel,
                         const QVector3D &accel);
    bool raw_data(const QVector3D &mag, bool hasAccel, const QVector3D &accel);
    QVector3D BpFast(int i);
    QVector3D acc_filt, mag_filt;

    /// Accelerometer reading paired with each solver buffer slot, indexed the
    /// same way as magcal.BpFast. The inherited MagCalibration_t is left alone;
    /// it is shared with the solver and knows nothing about accelerometers.
    QVector3D accelBuffer[MAGBUFFSIZE];
    bool accelBufferValid[MAGBUFFSIZE] = {false};

    MagQuality quality;
    MagQuality::Result metrics;

    GravityFit::Result gravity;
    void fitGravity();

    /// Leverage-based discard. Off by default: this is the one change in the
    /// quality work that alters what the solver sees, so it is opt-in until it
    /// has been compared against the inherited policy on real collections.
    Retention retentionPolicy = Retention::Inherited;
    MagRetention leverageRetention;

    /// A slot is on probation for a short while after being filled, so a
    /// transient bad calibration cannot immediately purge the evidence that
    /// would correct it.
    int addCounter = 0;
    int slotFilledAt[MAGBUFFSIZE] = {0};

    int evictions = 0;
    int leverageEvictions = 0;
    int outlierEvictions = 0;

    int chooseDiscardByLeverage();
};

#endif
