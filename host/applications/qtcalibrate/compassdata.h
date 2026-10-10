#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QList>
#include <QVector3D>
#include <QQuaternion>
#include <QObject>

#include <random>

#include "magcal/magcal.h"
#include "accelcalibration.h"
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

    /**
     * @brief Install magnetometer constants read back from a tag.
     *
     * @details The tag does not store the field magnitude B. It is recomputed
     *          as the mean calibrated magnitude of the buffered samples, and
     *          left as it was when the buffer is empty.
     */
    void setCalibrationConstants(float *V, float(*A)[3]);

    /**
     * @brief Metrics from the owned quality module, as of the last
     *        qualityUpdate().
     *
     * @details The only quality numbers now: the inherited ones they were
     *          compared against are gone with quality.c.
     */
    const MagQuality::Result &qualityMetrics() const { return metrics; }

    /**
     * @brief Fitted accelerometer zero-g offset, as of the last sample added.
     *
     * @details Applied when deriving dip and orientation, recorded in a
     *          capture, and written to the tag with the magnetometer constants.
     */
    const AccelCalibration::Result &accelOffset() const { return gravity; }

    /**
     * @brief Install an accelerometer offset read back from a tag, in mg.
     *
     * @details Marks the result valid with no fit statistics. It stands until
     *          the live population yields a valid fit, or the buffer is
     *          cleared.
     */
    void setAccelOffset(const QVector3D &offset);

    /// Evictions so far, and how many of them each rule decided.
    /// Both reset with the buffer.
    int evictionCount() const { return evictions; }
    int leverageEvictionCount() const { return leverageEvictions; }
    int outlierEvictionCount() const { return outlierEvictions; }

    void qualityUpdate();
    void clear();
 

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

    /*
     * The accelerometer calibrates itself from its own sample population,
     * which AccelCalibration owns. It used to be fitted from this class's
     * magnetometer buffer, which meant the magnetometer's retention policy
     * chose its samples -- and the two policies then converged on offsets
     * 3 mg apart, both about 4 mg from the fit over a whole capture.
     *
     * What is still paired here is accelBuffer: one accelerometer reading per
     * retained magnetometer sample, kept so that each sample's inclination
     * can be derived. That is a cross-check between the two sensors, not an
     * input to either calibration, which is why it lives beside the
     * magnetometer buffer rather than in either task.
     */
    AccelCalibration accelCal;
    AccelCalibration::Result gravity;
    /// gravity holds an offset loaded from the tag, not a live fit.
    bool gravityLoaded = false;

    /// Leverage and the studentized residual decide what to drop. This was
    /// opt-in while it was compared against the inherited nearest-pair scan;
    /// that scan is gone, so there is nothing to opt into.
    MagRetention leverageRetention;

    /// A slot is on probation for a short while after being filled, so a
    /// transient bad calibration cannot immediately purge the evidence that
    /// would correct it.
    int addCounter = 0;

    /// Tie-breaking for the inherited discard policy, and the fallback when a
    /// policy declines to choose.
    ///
    /// Seeded from a constant, and reseeded on every clear. The randomness is
    /// there to avoid a systematic bias in which of two equally redundant
    /// samples is dropped, and a fixed stream does that just as well as an
    /// unpredictable one -- while making a replay of one capture reproduce
    /// exactly, which is what lets a 0.03 difference between policies be told
    /// from run-to-run wobble. std::mt19937 rather than std::rand() because
    /// rand()'s sequence differs between platforms, and a replay should give
    /// the same answer on macOS and Windows.
    static constexpr unsigned int kDiscardSeed = 20261010u;
    std::mt19937 discardRng{kDiscardSeed};
    int slotFilledAt[MAGBUFFSIZE] = {0};

    int evictions = 0;
    int leverageEvictions = 0;
    int outlierEvictions = 0;

    int chooseDiscardByLeverage();
};

#endif
