/**
 * @file capture_replay.cc
 * @brief Replay a qtcalibrate sample capture through the calibration library.
 *
 * Answers questions about the calibration code without building or driving
 * qtcalibrate: feed a committed fixture straight into AccelCalibration and
 * MagQuality and print what they say, sample by sample.
 *
 * The questions this is for are the ones a single end-of-run number cannot
 * answer -- does the offset settle or wander, when does a metric stop
 * improving, does a change move anything -- and the replay in qtcalibrate is
 * a poor way to ask them: it needs the Qt build, a window, and a run in real
 * time, and it reports one line per two samples through a log.
 *
 * Build with -DBUILD_SENSORANALYSIS_CHECKS=ON. Unlike the checks beside it,
 * this is a tool and not an assertion: it takes a capture and prints, and
 * never decides anything is wrong.
 *
 *     capture_replay CAPTURE.json [--csv] [--every N] [--patches N]
 *
 * Without --csv it prints a periodic trace and a settling summary. With --csv
 * it prints one row per tick for plotting.
 */

#include "accelcalibration.h"
#include "compass_processor.h"
#include "compass_types.h"
#include "magquality.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

/// Matches qtcalibrate's calibration stream, where 1000 is one g.
const float kStreamOneG = 1000.0f;

struct Sample {
    QVector3D mag;
    QVector3D accel;
    bool hasAccel = false;
};

struct Capture {
    QVector<Sample> samples;
    CompassCalibration calibration;
    bool haveCalibration = false;
};

bool readCapture(const QString &path, Capture &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", qPrintable(path));
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        std::fprintf(stderr, "cannot parse %s: %s\n", qPrintable(path),
                     qPrintable(error.errorString()));
        return false;
    }
    const QJsonObject root = document.object();

    // A capture stores the solved constants as "offset" and "mapping", not
    // under the embedded v0/a00 names that fromMagnetometerJson() reads, so
    // build them directly rather than silently getting an identity.
    const QJsonObject calibration = root.value("calibration").toObject();
    const QJsonArray offset = calibration.value("offset").toArray();
    const QJsonArray mapping = calibration.value("mapping").toArray();
    if (calibration.value("valid").toBool()
        && offset.size() == 3 && mapping.size() == 3) {
        CompassCalibration::Matrix softIron{};
        bool shaped = true;
        for (int row = 0; row < 3; row++) {
            const QJsonArray values = mapping.at(row).toArray();
            if (values.size() != 3) {
                shaped = false;
                break;
            }
            for (int column = 0; column < 3; column++) {
                softIron[row][column] = values.at(column).toDouble();
            }
        }
        if (shaped) {
            out.calibration = CompassCalibration::fromMagnetometerConstants(
                QVector3D(offset.at(0).toDouble(), offset.at(1).toDouble(),
                          offset.at(2).toDouble()),
                softIron);
            out.haveCalibration = true;
        }
    }

    const QJsonArray samples = root.value("samples").toArray();
    out.samples.reserve(samples.size());
    for (const QJsonValue &value : samples) {
        const QJsonObject entry = value.toObject();
        const QJsonObject mag = entry.value("mag").toObject();
        Sample sample;
        sample.mag = QVector3D(mag.value("mx").toDouble(),
                               mag.value("my").toDouble(),
                               mag.value("mz").toDouble());
        sample.hasAccel = entry.value("has_accel").toBool();
        if (sample.hasAccel) {
            const QJsonObject accel = entry.value("accel").toObject();
            sample.accel = QVector3D(accel.value("ax").toDouble(),
                                     accel.value("ay").toDouble(),
                                     accel.value("az").toDouble());
        }
        out.samples.append(sample);
    }
    return !out.samples.isEmpty();
}

float robustSpread(QVector<float> values)
{
    if (values.isEmpty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const int n = values.size();
    const float median = (n % 2) ? values.at(n / 2)
                                 : 0.5f * (values.at(n / 2 - 1) + values.at(n / 2));
    QVector<float> deviations;
    deviations.reserve(n);
    for (float v : values) {
        deviations.append(std::fabs(v - median));
    }
    std::sort(deviations.begin(), deviations.end());
    const float mad = (n % 2)
        ? deviations.at(n / 2)
        : 0.5f * (deviations.at(n / 2 - 1) + deviations.at(n / 2));
    return 1.4826f * mad;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: capture_replay CAPTURE.json [--csv] [--every N] "
                     "[--patches N]\n");
        return 2;
    }

    QString path;
    bool csv = false;
    int every = 200;
    int patches = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--csv") == 0) {
            csv = true;
        } else if (std::strcmp(argv[i], "--every") == 0 && i + 1 < argc) {
            every = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--patches") == 0 && i + 1 < argc) {
            patches = std::max(1, std::atoi(argv[++i]));
        } else if (path.isEmpty()) {
            path = QString::fromLocal8Bit(argv[i]);
        }
    }

    Capture capture;
    if (!readCapture(path, capture)) {
        return 1;
    }

    AccelCalibration::Config config;
    if (patches > 0) {
        config.patches = patches;
    }
    AccelCalibration accel(config);

    if (csv) {
        std::printf("sample,offset_x,offset_y,offset_z,offset_mg,radius,"
                    "residual,patches_seen,isotropy,used,gated\n");
    } else {
        std::printf("%s: %d samples, calibration %s\n", qPrintable(path),
                    capture.samples.size(),
                    capture.haveCalibration ? "present" : "absent");
    }

    QVector<float> magnitudes;
    int fed = 0;
    for (const Sample &sample : capture.samples) {
        if (sample.hasAccel) {
            accel.add(sample.accel, kStreamOneG);
        }
        fed++;

        // One result per two samples, matching qtcalibrate's quality tick.
        if (fed % 2 != 0) {
            continue;
        }
        const AccelCalibration::Result r = accel.result(kStreamOneG);
        if (!r.valid) {
            continue;
        }
        magnitudes.append(r.offset.length());
        if (csv) {
            std::printf("%d,%.3f,%.3f,%.3f,%.3f,%.2f,%.3f,%d,%.4f,%d,%d\n",
                        fed, r.offset.x(), r.offset.y(), r.offset.z(),
                        r.offset.length(), r.radius, r.residual,
                        r.patchesSeen, r.isotropy, r.samples, r.gated);
        } else if (fed % every == 0) {
            std::printf("  sample %5d  |offset| %6.2f mg (%+7.2f %+7.2f %+7.2f)"
                        "  radius %6.1f  patches %2d/%2d  isotropy %.3f\n",
                        fed, r.offset.length(), r.offset.x(), r.offset.y(),
                        r.offset.z(), r.radius, r.patchesSeen, r.patches,
                        r.isotropy);
        }
    }

    const AccelCalibration::Result fitted = accel.result(kStreamOneG);
    if (csv) {
        return 0;
    }

    if (!fitted.valid) {
        std::printf("\nno valid fit: %d readings over %d/%d patches, "
                    "isotropy %.4f\n",
                    fitted.samples, fitted.patchesSeen, fitted.patches,
                    fitted.isotropy);
        return 0;
    }

    std::printf("\naccelerometer: offset %+.2f %+.2f %+.2f = %.2f mg, "
                "radius %.1f, residual %.2f\n",
                fitted.offset.x(), fitted.offset.y(), fitted.offset.z(),
                fitted.offset.length(), fitted.radius, fitted.residual);
    std::printf("               %d of %d readings used over %d/%d patches, "
                "isotropy %.3f, %d gated\n",
                fitted.samples, fitted.offered, fitted.patchesSeen, fitted.patches,
                fitted.isotropy, fitted.gated);

    // Whether the estimate settles is the question a single end-of-run number
    // cannot answer, and the one that caught the sliding-window fit.
    if (magnitudes.size() > 4) {
        const int half = magnitudes.size() / 2;
        float low = magnitudes.at(half);
        float high = magnitudes.at(half);
        for (int i = half; i < magnitudes.size(); i++) {
            low = std::min(low, magnitudes.at(i));
            high = std::max(high, magnitudes.at(i));
        }
        std::printf("               settling: %.2f to %.2f mg over the second "
                    "half, %.2f peak to peak\n", low, high, high - low);
    }

    // Inclination, from the same routine qtcalibrate uses, with and without
    // the offset applied -- which is what the accelerometer work is for.
    if (!capture.haveCalibration) {
        std::printf("\nno stored calibration in this capture; "
                    "skipping inclination\n");
        return 0;
    }
    // deriveSample(), not deriveCalibratedSample(): the magnetometer vectors
    // here are as the tag sent them, so the processor applies the capture's
    // own constants. deriveCalibratedSample() wants them already corrected,
    // which is how qtcalibrate calls it -- it calibrates before plotting.
    // Gated exactly as MagQuality gates it. Inclination means nothing for a
    // reading taken mid-swing, where the accelerometer is measuring motion
    // and not gravity, and ungated the spread comes out more than twice as
    // large -- which is a statement about the sweep, not the calibration.
    const MagQuality::Config quality;
    CompassProcessor processor{capture.calibration};
    QVector<float> raw;
    QVector<float> corrected;
    int gatedOut = 0;
    for (const Sample &sample : capture.samples) {
        if (!sample.hasAccel) {
            continue;
        }
        const QVector3D settled = sample.accel - fitted.offset;
        if (std::fabs(settled.length() - kStreamOneG)
            > quality.accelGate * kStreamOneG) {
            gatedOut++;
            continue;
        }
        CompassRawSample input;
        input.mag = sample.mag;
        CompassDerivedSample derived;

        input.accel = sample.accel;
        if (processor.deriveSample(input, derived)) {
            raw.append(derived.dip);
        }
        input.accel = settled;
        if (processor.deriveSample(input, derived)) {
            corrected.append(derived.dip);
        }
    }
    std::printf("\ninclination spread (robust): %.3f deg uncorrected, "
                "%.3f deg with the offset removed  (%d in gate, %d out)\n",
                robustSpread(raw), robustSpread(corrected),
                corrected.size(), gatedOut);
    return 0;
}
