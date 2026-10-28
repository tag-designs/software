/**
 * @file    calibration_dump.cc
 * @brief   Prints every result of the calibration classes, exactly, for a
 *          before-and-after comparison of a change that must not alter them.
 *
 * @details Drives Directions, MagQuality, MagRetention, GravityFit and
 *          AccelCalibration through their public API over both qtcalibrate
 *          capture fixtures and seeded synthetic data, and prints each number
 *          as a hexadecimal float (%a), so two runs agree as text exactly when
 *          they agree to the bit. It makes no judgement of its own: build it
 *          on the code before a change, save the output, build it on the code
 *          after, and diff.
 *
 *              calibration_dump > before.txt      (old code)
 *              calibration_dump > after.txt       (new code)
 *              diff before.txt after.txt          (must be empty)
 *
 *          This is how the move of these classes onto the Qt-free core was
 *          shown to change nothing (see test/README.md). The magnetometer
 *          samples are calibrated with each fixture's own stored calibration
 *          and the dip derived by CompassProcessor, as qtcalibrate feeds
 *          MagQuality. Synthetic data comes from a fixed std::mt19937 stream
 *          through integer arithmetic only, so it does not depend on a
 *          library's distribution classes.
 *
 *          Build with -DBUILD_SENSORANALYSIS_CHECKS=ON. Takes no arguments;
 *          the fixtures are found under the source tree compiled in.
 */

#include "accelcalibration.h"
#include "compass_processor.h"
#include "compass_types.h"
#include "directions.h"
#include "gravityfit.h"
#include "magquality.h"
#include "magretention.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <QVector3D>

#include <cstdio>
#include <random>

namespace {

/// One g in qtcalibrate's calibration stream units (milli-g).
const float kOneG = 1000.0f;

/// One fixture sample: raw magnetometer, accelerometer if present.
struct Sample {
    QVector3D mag;      ///< Raw magnetometer, µT.
    QVector3D accel;    ///< Accelerometer, mg; meaningful when hasAccel.
    bool hasAccel = false;
};

/// A fixture: its samples and its stored calibration.
struct Fixture {
    QString name;                   ///< File name, for the headings.
    QVector<Sample> samples;        ///< In capture order.
    CompassCalibration calibration; ///< The fixture's own fit.
};

/**
 * @brief Loads a qtcalibrate capture fixture.
 * @param[in] path  Path relative to the source root.
 * @return The fixture; empty samples when it cannot be read.
 */
Fixture load(const QString &path)
{
    Fixture f;
    f.name = QFileInfo(path).fileName();
    QFile file(QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", qPrintable(path));
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
    const QJsonArray off = cal.value("offset").toArray();
    const QJsonArray map = cal.value("mapping").toArray();
    CompassCalibration::Matrix soft;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            soft[i][j] = map.at(i).toArray().at(j).toDouble();
        }
    }
    f.calibration = CompassCalibration::fromMagnetometerConstants(
        QVector3D(off.at(0).toDouble(), off.at(1).toDouble(), off.at(2).toDouble()), soft);
    return f;
}

/// Prints a labelled float exactly.
void put(const char *label, double value)
{
    std::printf(" %s=%a", label, value);
}

/// Prints a labelled vector exactly.
void put(const char *label, const QVector3D &v)
{
    std::printf(" %s=(%a,%a,%a)", label, double(v.x()), double(v.y()), double(v.z()));
}

/// Prints every field of a MagQuality result.
void dump(const MagQuality::Result &r)
{
    std::printf(" seen=%d of=%d min=%d max=%d cells=%d apatches=%d dipn=%d acc=%d n=%d have=%d",
                r.patchesSeen, r.magPatches, r.patchMin, r.patchMax, r.attitudeCells, r.attitudePatches,
                r.dipSamples, r.accelSamples, r.samples, int(r.haveAccelMetrics));
    put("cov", r.coverage);
    put("iso", r.isotropy);
    put("div", r.attitudeDiversity);
    put("field", r.field);
    put("fit", r.fitError);
    put("spread", r.residualSpread);
    put("p95", r.residualP95);
    put("hard", r.residualHardIron);
    put("dip", r.dipMeanDeg);
    put("dipsd", r.dipSpreadDeg);
    put("dip95", r.dipP95Deg);
    std::printf("\n");
}

/// Prints every field of an AccelCalibration result.
void dump(const AccelCalibration::Result &r)
{
    std::printf(" valid=%d n=%d seen=%d of=%d offered=%d gated=%d", int(r.valid), r.samples, r.patchesSeen,
                r.patches, r.offered, r.gated);
    put("off", r.offset);
    put("radius", r.radius);
    put("res", r.residual);
    put("cov", r.coverage);
    put("iso", r.isotropy);
    std::printf("\n");
}

/// Prints every field of a GravityFit result.
void dump(const GravityFit::Result &r)
{
    std::printf(" valid=%d n=%d", int(r.valid), r.samples);
    put("off", r.offset);
    put("radius", r.radius);
    put("res", r.residual);
    std::printf("\n");
}

/// Prints a retention choice and every leverage.
void dumpRetention(const char *label, const MagRetention &retention, const QVector<QVector3D> &samples,
                   const QVector<bool> &probation)
{
    const MagRetention::Choice c = retention.choose(samples, probation);
    std::printf("%s n=%d choice=%d reason=%d", label, int(samples.size()), c.index, int(c.reason));
    put("score", c.score);
    const QVector<float> h = retention.leverages(samples);
    std::printf(" leverages=%d:", int(h.size()));
    for (float v : h) {
        std::printf(" %a", double(v));
    }
    std::printf("\n");
}

/// Directions over every sample of a fixture, and the lattices used.
void dumpDirections(const Fixture &f)
{
    std::printf("== directions %s\n", qPrintable(f.name));
    for (int count : {1, 16, 32, 100}) {
        for (int i = -1; i <= count; i++) {
            std::printf("centre %d/%d", i, count);
            put("c", Directions::patchCenter(i, count));
            std::printf("\n");
        }
    }
    double moment[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (int i = 0; i < f.samples.size(); i++) {
        const Sample &s = f.samples.at(i);
        std::printf("patch %d m16=%d m100=%d a32=%d", i, Directions::patch(s.mag, 16),
                    Directions::patch(s.mag, 100), Directions::patch(s.accel, 32));
        Directions::accumulate(moment, s.mag);
        put("iso", Directions::isotropy(moment));
        double eig[3];
        Directions::symmetricEigenvalues(moment, eig);
        put("e0", eig[0]);
        put("e1", eig[1]);
        put("e2", eig[2]);
        std::printf("\n");
    }
}

/// MagQuality fed as qtcalibrate feeds it, at intervals through the capture.
void dumpQuality(const Fixture &f)
{
    std::printf("== magquality %s\n", qPrintable(f.name));
    const CompassProcessor processor{CompassCalibration()};
    MagQuality quality;
    MagQuality bare;   // the same samples with no accelerometer
    for (int i = 0; i < f.samples.size(); i++) {
        const Sample &s = f.samples.at(i);
        const QVector3D point = f.calibration.apply(s.mag);
        bare.add(point);
        CompassRawSample raw;
        raw.mag = point;
        raw.accel = s.accel;
        CompassDerivedSample derived;
        if (s.hasAccel && processor.deriveCalibratedSample(raw, derived)) {
            quality.add(point, &s.accel, derived.dip, kOneG);
        } else {
            quality.add(point);
        }
        if (i % 25 == 24 || i + 1 == f.samples.size()) {
            std::printf("q %d", i);
            dump(quality.result());
            std::printf("b %d", i);
            dump(bare.result());
        }
    }
    MagQuality::Config cfg;
    cfg.magPatches = 37;
    cfg.gravityPatches = 40;   // clamped to 32
    cfg.accelGate = 0.03f;
    MagQuality odd(cfg);
    for (const Sample &s : f.samples) {
        odd.add(f.calibration.apply(s.mag), s.hasAccel ? &s.accel : nullptr, 60.0f, kOneG);
    }
    std::printf("odd gp=%d", odd.config().gravityPatches);
    dump(odd.result());
    std::printf("rejects %d %d %d\n", int(odd.add(QVector3D())),
                int(odd.add(QVector3D(qInf(), 0, 0))), int(odd.add(QVector3D(1, 2, 3), nullptr)));
}

/// MagRetention over windows of a fixture, as qtcalibrate's buffer would hold them.
void dumpRetention(const Fixture &f)
{
    std::printf("== magretention %s\n", qPrintable(f.name));
    QVector<QVector3D> points;
    for (const Sample &s : f.samples) {
        points.append(f.calibration.apply(s.mag));
    }
    MagRetention::Config noOutliers;
    noOutliers.rejectOutliers = false;
    MagRetention::Config noFloor;
    noFloor.patchCount = 0;
    const MagRetention standard, leverageOnly(noOutliers), floorless(noFloor);
    for (int start = 0; start < points.size(); start += 97) {
        for (int len : {5, 10, 11, 60, 300}) {
            if (start + len > points.size()) {
                continue;
            }
            const QVector<QVector3D> window = points.mid(start, len);
            QVector<bool> probation(len, false);
            for (int i = len - std::min(len, 20); i < len; i++) {
                probation[i] = true;
            }
            char label[64];
            std::snprintf(label, sizeof label, "w %d+%d std", start, len);
            dumpRetention(label, standard, window, QVector<bool>());
            std::snprintf(label, sizeof label, "w %d+%d prob", start, len);
            dumpRetention(label, standard, window, probation);
            std::snprintf(label, sizeof label, "w %d+%d lev", start, len);
            dumpRetention(label, leverageOnly, window, QVector<bool>());
            std::snprintf(label, sizeof label, "w %d+%d nofloor", start, len);
            dumpRetention(label, floorless, window, probation);
        }
    }
    // Raw (uncalibrated) samples and a planted gross outlier.
    QVector<QVector3D> raw;
    for (const Sample &s : f.samples.mid(0, 200)) {
        raw.append(s.mag);
    }
    dumpRetention("raw200", standard, raw, QVector<bool>());
    QVector<QVector3D> planted = points.mid(0, 200);
    planted[77] = planted[77] * 1.6f;
    dumpRetention("planted", standard, planted, QVector<bool>());
}

/// GravityFit's two passes and AccelCalibration streaming, over a fixture.
void dumpAccel(const Fixture &f)
{
    std::printf("== gravityfit %s\n", qPrintable(f.name));
    GravityFit fit;
    int accepted = 0;
    for (const Sample &s : f.samples) {
        if (s.hasAccel) {
            accepted += fit.add(s.accel, kOneG) ? 1 : 0;
        }
    }
    std::printf("pass1 accepted=%d ready=%d", accepted, int(fit.ready()));
    const GravityFit::Result first = fit.result(kOneG);
    dump(first);
    fit.refit(first.offset);
    accepted = 0;
    for (const Sample &s : f.samples) {
        if (s.hasAccel) {
            accepted += fit.add(s.accel, kOneG) ? 1 : 0;
        }
    }
    std::printf("pass2 accepted=%d", accepted);
    dump(fit.result(kOneG));
    std::printf("wrong-g");
    dump(fit.result(3000.0f));

    std::printf("== accelcalibration %s\n", qPrintable(f.name));
    AccelCalibration cal;
    for (int i = 0; i < f.samples.size(); i++) {
        const Sample &s = f.samples.at(i);
        if (!s.hasAccel) {
            continue;
        }
        const bool took = cal.add(s.accel, kOneG);
        if (i % 10 == 9) {
            std::printf("a %d took=%d", i, int(took));
            dump(cal.result(kOneG));   // refreshes the gate offset, as qtcalibrate's tick does
        }
    }
    std::printf("occupancy");
    for (bool b : cal.occupancy()) {
        std::printf(" %d", int(b));
    }
    std::printf("\n");
}

/// A uniform value in [lo, hi) from the generator, by integer arithmetic.
float uniform(std::mt19937 &rng, float lo, float hi)
{
    return lo + (hi - lo) * float(rng() >> 8) / float(1u << 24);
}

/// The same classes on seeded synthetic spheres: offsets, noise, flat sweeps.
void dumpSynthetic()
{
    std::printf("== synthetic\n");
    std::mt19937 rng(20261028u);
    for (int run = 0; run < 40; run++) {
        const float radius = uniform(rng, 20.0f, 65.0f);
        const QVector3D offset(uniform(rng, -3, 3), uniform(rng, -3, 3), uniform(rng, -3, 3));
        const float noise = uniform(rng, 0.0f, 0.04f);
        const bool flat = run % 5 == 0;
        const int n = 30 + int(rng() % 600);
        MagQuality quality;
        AccelCalibration accel;
        GravityFit gravity;
        QVector<QVector3D> points;
        for (int i = 0; i < n; i++) {
            QVector3D u(uniform(rng, -1, 1), uniform(rng, -1, 1), flat ? uniform(rng, -0.05f, 0.05f) : uniform(rng, -1, 1));
            if (u.lengthSquared() < 1e-6f) {
                continue;
            }
            u.normalize();
            const QVector3D p = u * radius * (1.0f + noise * uniform(rng, -1, 1)) + offset;
            const QVector3D a = u * kOneG * (1.0f + noise * uniform(rng, -1, 1)) + offset * 10.0f;
            points.append(p);
            quality.add(p, &a, uniform(rng, 55, 70), kOneG);
            accel.add(a, kOneG);
            gravity.add(a, kOneG);
        }
        std::printf("s %d q", run);
        dump(quality.result());
        std::printf("s %d a", run);
        dump(accel.result(kOneG));
        std::printf("s %d g", run);
        dump(gravity.result(kOneG));
        char label[32];
        std::snprintf(label, sizeof label, "s %d r", run);
        dumpRetention(label, MagRetention(), points, QVector<bool>());
    }
    // Every sample at one radius, which drives MagRetention's fallback scale.
    QVector<QVector3D> sphere;
    for (int i = 0; i < 40; i++) {
        sphere.append(Directions::patchCenter(i, 40) * 50.0f);
    }
    dumpRetention("one-radius", MagRetention(), sphere, QVector<bool>());
}

} // namespace

/**
 * @brief Prints the dump to stdout.
 * @return 0, or 1 when a fixture cannot be read.
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
            return 1;
        }
        dumpDirections(f);
        dumpQuality(f);
        dumpRetention(f);
        dumpAccel(f);
    }
    dumpSynthetic();
    return 0;
}
