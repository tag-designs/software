/**
 * @file    calibration_session_check.cc
 * @brief   Requires calibcore::CalibrationSession's live compass and loaded
 *          constants to be bit-identical to the qtcalibrate code they
 *          replaced.
 *
 * @details CompassData in qtcalibrate did this work on Qt types until it
 *          became an adapter over CalibrationSession. qtcalibrate's replay
 *          logs cover the session's sample intake, solver, retention, quality
 *          figures and accelerometer fit, and were byte-identical across the
 *          move; they do not record the live compass, and a replay never loads
 *          constants from a tag. This checks those two paths against verbatim
 *          copies of the code as it stood (do not edit the references to match
 *          the session):
 *
 *            - the filtered eCompass, sample by sample over both capture
 *              fixtures, with and without an accelerometer offset in force;
 *            - the field recomputed when constants are loaded, and the
 *              calibrated points, over a buffer filled from each fixture.
 *
 *          Every output is compared as bits. Build with
 *          -DBUILD_SENSORANALYSIS_CHECKS=ON; takes no arguments and exits
 *          non-zero on any difference.
 */

#include "calibration_session.h"
#include "compass_processor.h"
#include "compass_types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QVector>
#include <QVector3D>

#include <cstdio>
#include <cstring>

namespace {

/// One fixture sample.
struct Sample {
    QVector3D mag;          ///< Raw magnetometer, µT.
    QVector3D accel;        ///< Accelerometer, mg.
    bool hasAccel = false;  ///< accel is a reading.
};

/// A fixture's samples and its stored constants, as float, as the tag holds them.
struct Fixture {
    QString name;            ///< File name.
    QVector<Sample> samples; ///< In capture order.
    float V[3];              ///< Hard iron, µT.
    float A[3][3];           ///< Inverse soft iron.
};

/**
 * @brief   Loads a capture fixture.
 * @param[in] path  Relative to the source root.
 * @return  The fixture; no samples when it cannot be read.
 */
Fixture load(const QString &path)
{
    Fixture f;
    f.name = QFileInfo(path).fileName();
    QFile file(QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return f;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : root.value("samples").toArray()) {
        const QJsonObject s = v.toObject();
        const QJsonObject m = s.value("mag").toObject();
        Sample sample;
        sample.mag = QVector3D(m.value("mx").toDouble(), m.value("my").toDouble(), m.value("mz").toDouble());
        sample.hasAccel = s.value("has_accel").toBool();
        if (sample.hasAccel) {
            const QJsonObject a = s.value("accel").toObject();
            sample.accel = QVector3D(a.value("ax").toDouble(), a.value("ay").toDouble(), a.value("az").toDouble());
        }
        f.samples.append(sample);
    }
    const QJsonObject cal = root.value("calibration").toObject();
    for (int i = 0; i < 3; i++) {
        f.V[i] = static_cast<float>(cal.value("offset").toArray().at(i).toDouble());
        for (int j = 0; j < 3; j++) {
            f.A[i][j] = static_cast<float>(cal.value("mapping").toArray().at(i).toArray().at(j).toDouble());
        }
    }
    return f;
}

// ---- The reference: qtcalibrate's CompassData code, verbatim ---------------

/// calibrationFromMagcal(), with the solver's V and invW passed in.
CompassCalibration referenceCalibration(const float V[3], const float invW[3][3])
{
    CompassCalibration::Matrix softIron;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            softIron[i][j] = invW[i][j];
        }
    }

    return CompassCalibration::fromMagnetometerConstants(
        QVector3D(V[0], V[1], V[2]),
        softIron);
}

/// CompassData::eCompass() and the filter state it kept.
struct ReferenceCompass {
    QVector3D acc_filt, mag_filt;   ///< The filter, as CompassData held it.
    QVector3D gravityOffset;        ///< gravity.offset.
    bool gravityValid = false;      ///< gravity.valid.
    CompassCalibration calibration; ///< calibrationFromMagcal().

    /// The method as it stood, with apply_calibration() inlined.
    bool eCompass(QVector3D magin, QVector3D accel, QQuaternion &q,
                  float& dip, float& field)
    {
        float alpha = 0.4; // filter coefficient

        magin = calibration.apply(magin);

        if (gravityValid) {
            accel -= gravityOffset;
        }
        acc_filt = alpha * acc_filt + (1.0-alpha) * accel;
        mag_filt = alpha * mag_filt + (1.0-alpha) * magin;

        accel = acc_filt;
        magin = mag_filt;

        CompassRawSample raw;
        raw.accel = accel;
        raw.mag = magin;

        CompassProcessor processor{CompassCalibration()};
        CompassDerivedSample derived;
        if (!processor.deriveCalibratedSample(raw, derived))
            return false;

        q = derived.q;
        dip = derived.dip;
        field = derived.field;
        return true;
    }
};

// ---- Comparison --------------------------------------------------------------

int failures = 0;   ///< Differences found.
int compared = 0;   ///< Values compared.

/// Records a difference unless @p a and @p b have the same bits.
void same(float a, float b, const char *what, const QString &where)
{
    compared++;
    if (std::memcmp(&a, &b, sizeof a) != 0) {
        if (failures < 20) {
            std::printf("DIFF %s at %s: %a vs %a\n", what, qPrintable(where), double(a), double(b));
        }
        failures++;
    }
}

/// The live compass over one fixture, reference against session.
void checkCompass(const Fixture &f, bool withOffset)
{
    const QVector3D offset(-29.4f, -5.2f, -67.1f);
    ReferenceCompass ref;
    ref.calibration = referenceCalibration(f.V, f.A);
    ref.gravityValid = withOffset;
    ref.gravityOffset = offset;

    calibcore::CalibrationSession session;
    session.setConstants(f.V, f.A);
    if (withOffset) {
        session.setAccelOffset(toVec3f(offset));
    }

    for (int i = 0; i < f.samples.size(); i++) {
        const Sample &s = f.samples.at(i);
        QQuaternion q;
        float dip = 0.0f, field = 0.0f;
        const bool refOk = ref.eCompass(s.mag, s.accel, q, dip, field);
        compasscore::Orientation o;
        const bool ok = session.orientation(toVec3f(s.mag), toVec3f(s.accel), o);
        const QString where = QString("%1 %2 sample %3").arg(f.name, withOffset ? "offset" : "plain").arg(i);
        compared++;
        if (refOk != ok) {
            std::printf("DIFF success at %s\n", qPrintable(where));
            failures++;
            continue;
        }
        if (!ok) {
            continue;
        }
        same(q.scalar(), o.q.w, "q.w", where);
        same(q.x(), o.q.x, "q.x", where);
        same(q.y(), o.q.y, "q.y", where);
        same(q.z(), o.q.z, "q.z", where);
        same(dip, o.dip, "dip", where);
        same(field, o.field, "field", where);
    }
}

/// The field recomputed on loading constants, and the calibrated points,
/// over a buffer the fixture filled.
void checkLoadedField(const Fixture &f)
{
    calibcore::CalibrationSession session;
    for (const Sample &s : f.samples) {
        session.addSample(toVec3f(s.mag), s.hasAccel, toVec3f(s.accel));
    }
    session.setConstants(f.V, f.A);

    // CompassData::setCalibrationConstants(), from "B is not stored".
    const MagCalibration_t &magcal = session.solver();
    const CompassCalibration calibration = referenceCalibration(f.V, f.A);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
            const QVector3D raw(magcal.BpFast[0][i], magcal.BpFast[1][i], magcal.BpFast[2][i]);
            sum += calibration.apply(raw).length();
            count++;
        }
    }
    float B = 50.0f;
    if (count > 0) {
        B = sum / count;
    }
    same(B, session.field(), "loaded B", f.name);

    const std::vector<sensormath::Vec3f> points = session.calibratedPoints();
    int k = 0;
    for (int i = 0; i < MAGBUFFSIZE; i++) {
        if (!magcal.valid[i]) {
            continue;
        }
        const QVector3D raw(magcal.BpFast[0][i], magcal.BpFast[1][i], magcal.BpFast[2][i]);
        const QVector3D p = calibration.apply(raw);
        const QString where = QString("%1 point %2").arg(f.name).arg(i);
        same(p.x(), points[k].x, "point.x", where);
        same(p.y(), points[k].y, "point.y", where);
        same(p.z(), points[k].z, "point.z", where);
        k++;
    }
}

} // namespace

/**
 * @brief   Runs every comparison.
 * @return  0 when all are identical; 1 otherwise, or when a fixture is missing.
 */
int main()
{
    const QStringList fixtures = {
        "host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json",
        "host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-163303.json",
    };
    for (const QString &path : fixtures) {
        const Fixture f = load(path);
        if (f.samples.isEmpty()) {
            std::printf("cannot read %s\n", qPrintable(path));
            return 1;
        }
        checkCompass(f, false);
        checkCompass(f, true);
        checkLoadedField(f);
    }
    std::printf("%d values compared, %d differ\n", compared, failures);
    return failures == 0 ? 0 : 1;
}
