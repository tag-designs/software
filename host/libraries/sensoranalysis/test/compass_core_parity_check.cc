/**
 * @file compass_core_parity_check.cc
 * @brief Bit-for-bit check of the Qt-free compass core against the Qt code it
 *        replaced.
 *
 * @details compasscore::solveOrientation() and applyMagnetometer() are a port
 *          of CompassProcessor::computeOrientation() and
 *          CompassCalibration::apply() from QVector3D/QQuaternion onto
 *          sensormath. This program keeps a verbatim copy of that Qt code as
 *          the reference and requires every output -- the quaternion's four
 *          parts, dip, field and acceleration magnitude, and the success flag
 *          -- to be bit-identical across:
 *
 *          - every sample of the qtcalibrate capture fixtures, with the
 *            calibration stored in each capture and with no calibration;
 *          - seeded random sensor pairs under random calibrations, covering
 *            both accelerometer branches and both magnetometer branches;
 *          - edge cases: zero vectors, vectors within 1e-5 of unit length
 *            (which Qt's normalize() leaves alone), and tiny vectors.
 *
 *          When vecmath.h is moved onto glm or Eigen, this check is how the
 *          change is measured: run it with --report to print the largest
 *          difference per output instead of failing on the first. In that
 *          report, inf means one side produced NaN where the other did not.
 */

#include "compass_core.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{

int failures = 0;
long comparisons = 0;
bool report = false;
std::array<double, 7> worst{}; // qw, qx, qy, qz, dip, field, mg

const char *const kOutputNames[] = {"qw", "qx", "qy", "qz", "dip", "field", "mg"};

using Matrix = std::array<std::array<double, 3>, 3>;

// ---------------------------------------------------------------------------
// Reference: the Qt implementation, copied verbatim from compass_types.cpp and
// compass_processor.cpp as they stood before the core existed. Do not edit
// these to match the core; they are what the core must match.
// ---------------------------------------------------------------------------

QVector3D qtApply(const QVector3D &hardIron_, const Matrix &softIron_, const QVector3D &mag)
{
    const double x = mag.x() - hardIron_.x();
    const double y = mag.y() - hardIron_.y();
    const double z = mag.z() - hardIron_.z();

    return QVector3D(
        x * softIron_[0][0] + y * softIron_[0][1] + z * softIron_[0][2],
        x * softIron_[1][0] + y * softIron_[1][1] + z * softIron_[1][2],
        x * softIron_[2][0] + y * softIron_[2][1] + z * softIron_[2][2]);
}

bool qtComputeOrientation(
    QVector3D mag,
    QVector3D accel,
    QQuaternion &q,
    float &dip,
    float &field,
    float &mg,
    bool applyCalibration,
    const QVector3D &hardIron,
    const Matrix &softIron)
{
    QQuaternion qacc;
    float q0, q1, q2, q3;

    if (applyCalibration) {
        mag = qtApply(hardIron, softIron, mag);
    }

    if ((mag.length() == 0.0) || (accel.length() == 0.0)) {
        return false;
    }

    field = mag.length();
    mg = accel.length();
    accel.normalize();
    mag.normalize();

    accel = QVector3D(accel[0], -accel[1], accel[2]);
    mag = QVector3D(mag[0], mag[1], -mag[2]);

    if (accel.z() >= 0) {
        q0 = sqrt((accel.z() + 1) * 0.5);
        q1 = -accel.y() / (2.0 * q0);
        q2 = accel.x() / (2.0 * q0);
        q3 = 0;
    } else {
        double x = sqrt((1 - accel.z()) * 0.5);
        q0 = -accel.y() / (2.0 * x);
        q1 = x;
        q2 = 0;
        q3 = accel.x() / (2.0 * x);
    }

    qacc = QQuaternion(q0, q2, q1, q3);
    qacc.normalize();

    QVector3D rotatedMag = qacc.rotatedVector(mag);

    float gamma = rotatedMag[0] * rotatedMag[0] + rotatedMag[1] * rotatedMag[1];
    float beta = sqrt(gamma + rotatedMag[0] * sqrt(gamma));
    float betaneg = sqrt(gamma - rotatedMag[0] * sqrt(gamma));
    float q0_mag, q3_mag;

    if (rotatedMag[0] >= 0.0) {
        q0_mag = beta / sqrt(2.0 * gamma);
        q3_mag = rotatedMag[1] / (sqrt(2.0) * beta);
    } else {
        q0_mag = rotatedMag[1] / (sqrt(2.0) * betaneg);
        q3_mag = betaneg / sqrt(2.0 * gamma);
    }

    QQuaternion qmag(q0_mag, 0, 0, -q3_mag);
    qmag = QQuaternion(sqrt(2) / 2, 0, 0, sqrt(2) / 2) * qmag;
    qmag.normalize();

    q = qacc * qmag;
    q.normalize();
    q.setX(-q.x());
    q.setY(-q.y());

    dip = atan2(rotatedMag[2], sqrt(gamma)) * 180.0 / M_PI;
    return true;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool sameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

void fail(const char *what, int index, const char *output, float qt, float core)
{
    failures++;
    if (failures <= 10) {
        std::printf("FAIL: %s case %d %s: Qt %.9g core %.9g\n", what, index, output, qt, core);
    }
}

void compare(const char *what, int index, int output, float qt, float core)
{
    if (report) {
        const double diff = std::fabs(double(qt) - double(core));
        if (diff > worst[output] || (std::isnan(qt) != std::isnan(core))) {
            worst[output] = std::isnan(diff) ? INFINITY : diff;
        }
        return;
    }
    if (!sameBits(qt, core)) {
        fail(what, index, kOutputNames[output], qt, core);
    }
}

compasscore::CalibrationConstants coreConstants(const QVector3D &hardIron, const Matrix &softIron)
{
    compasscore::CalibrationConstants constants;
    constants.hardIron = {hardIron.x(), hardIron.y(), hardIron.z()};
    constants.softIron = softIron;
    return constants;
}

/// Runs one sensor pair through both implementations and compares every output.
void checkPair(
    const char *what,
    int index,
    const QVector3D &mag,
    const QVector3D &accel,
    bool applyCalibration,
    const QVector3D &hardIron,
    const Matrix &softIron)
{
    comparisons++;

    // Calibration on its own.
    const compasscore::CalibrationConstants constants = coreConstants(hardIron, softIron);
    const QVector3D qtCal = qtApply(hardIron, softIron, mag);
    const sensormath::Vec3f coreCal =
        compasscore::applyMagnetometer(constants, {mag.x(), mag.y(), mag.z()});
    if (!report
        && (!sameBits(qtCal.x(), coreCal.x) || !sameBits(qtCal.y(), coreCal.y)
            || !sameBits(qtCal.z(), coreCal.z))) {
        fail(what, index, "calibrated mag", qtCal.x(), coreCal.x);
    }

    // The solve.
    QQuaternion qtQ;
    float qtDip = 0.0f;
    float qtField = 0.0f;
    float qtMg = 0.0f;
    const bool qtOk = qtComputeOrientation(
        mag, accel, qtQ, qtDip, qtField, qtMg, applyCalibration, hardIron, softIron);

    compasscore::Orientation core;
    const bool coreOk = compasscore::solveOrientation(
        {mag.x(), mag.y(), mag.z()},
        {accel.x(), accel.y(), accel.z()},
        applyCalibration ? &constants : nullptr,
        core);

    if (qtOk != coreOk) {
        failures++;
        std::printf("FAIL: %s case %d: Qt %s, core %s\n",
                    what, index, qtOk ? "solved" : "refused", coreOk ? "solved" : "refused");
        return;
    }
    if (!qtOk) {
        return;
    }
    compare(what, index, 0, qtQ.scalar(), core.q.w);
    compare(what, index, 1, qtQ.x(), core.q.x);
    compare(what, index, 2, qtQ.y(), core.q.y);
    compare(what, index, 3, qtQ.z(), core.q.z);
    compare(what, index, 4, qtDip, core.dip);
    compare(what, index, 5, qtField, core.field);
    compare(what, index, 6, qtMg, core.mg);
}

const Matrix kIdentity{{{{1.0, 0.0, 0.0}}, {{0.0, 1.0, 0.0}}, {{0.0, 0.0, 1.0}}}};

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

int checkCapture(const char *relativePath)
{
    QString path = QString::fromUtf8(relativePath);
#ifdef TAG_DESIGNS_SOURCE_DIR
    path = QStringLiteral(TAG_DESIGNS_SOURCE_DIR "/") + path;
#endif
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        failures++;
        std::printf("FAIL: cannot open %s\n", qPrintable(path));
        return 0;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject calibration = root.value(QStringLiteral("calibration")).toObject();
    const QJsonArray offset = calibration.value(QStringLiteral("offset")).toArray();
    const QJsonArray mapping = calibration.value(QStringLiteral("mapping")).toArray();
    const QVector3D hardIron(offset.at(0).toDouble(), offset.at(1).toDouble(), offset.at(2).toDouble());
    Matrix softIron = kIdentity;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            softIron[r][c] = mapping.at(r).toArray().at(c).toDouble();
        }
    }

    int count = 0;
    for (const QJsonValue &value : root.value(QStringLiteral("samples")).toArray()) {
        const QJsonObject sample = value.toObject();
        if (!sample.value(QStringLiteral("has_accel")).toBool()) {
            continue;
        }
        const QJsonObject m = sample.value(QStringLiteral("mag")).toObject();
        const QJsonObject a = sample.value(QStringLiteral("accel")).toObject();
        const QVector3D mag(m.value(QStringLiteral("mx")).toDouble(),
                            m.value(QStringLiteral("my")).toDouble(),
                            m.value(QStringLiteral("mz")).toDouble());
        const QVector3D accel(a.value(QStringLiteral("ax")).toDouble(),
                              a.value(QStringLiteral("ay")).toDouble(),
                              a.value(QStringLiteral("az")).toDouble());
        checkPair(relativePath, count, mag, accel, true, hardIron, softIron);
        checkPair(relativePath, count, mag, accel, false, hardIron, softIron);
        count++;
    }
    return count;
}

void checkRandom(int count)
{
    std::mt19937 rng(20261020);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> gravity(800.0f, 1200.0f);
    std::uniform_real_distribution<float> field(20.0f, 70.0f);
    std::uniform_real_distribution<double> skew(-0.1, 0.1);
    std::uniform_real_distribution<float> offset(-300.0f, 300.0f);

    auto direction = [&]() {
        QVector3D v;
        do {
            v = QVector3D(unit(rng), unit(rng), unit(rng));
        } while (v.length() < 0.1f);
        return v.normalized();
    };

    for (int i = 0; i < count; i++) {
        const QVector3D hardIron(offset(rng), offset(rng), offset(rng));
        Matrix softIron = kIdentity;
        for (auto &row : softIron) {
            for (double &cell : row) {
                cell += skew(rng);
            }
        }
        const QVector3D accel = direction() * gravity(rng);
        const QVector3D mag = direction() * field(rng) + hardIron;
        checkPair("random", i, mag, accel, true, hardIron, softIron);
        checkPair("random", i, mag - hardIron, accel, false, hardIron, softIron);
    }
}

void checkEdges()
{
    const QVector3D zero(0.0f, 0.0f, 0.0f);
    const QVector3D down(0.0f, 0.0f, 1000.0f);
    const QVector3D up(0.0f, 0.0f, -1000.0f);
    const QVector3D north(30.0f, 0.0f, 40.0f);
    const QVector3D south(-30.0f, 0.0f, 40.0f);
    // Lengths 1, 1 + 5e-6 and 1 + 2e-5: Qt's vector normalize() skips the
    // first two and divides the third.
    const QVector3D unitA(0.6f, 0.8f, 0.0f);
    const QVector3D nearUnit(0.6f * 1.000005f, 0.8f * 1.000005f, 0.0f);
    const QVector3D justOff(0.6f * 1.00002f, 0.8f * 1.00002f, 0.0f);
    const QVector3D tiny(1e-6f, 2e-6f, -1e-6f);

    const QVector3D cases[][2] = {
        {north, zero}, {zero, down}, {zero, zero},
        {north, down}, {north, up}, {south, down}, {south, up},
        {unitA, down}, {nearUnit, down}, {justOff, down},
        {north, unitA}, {north, nearUnit}, {north, justOff},
        {tiny, down}, {north, tiny}, {tiny, tiny},
        {QVector3D(0.0f, 30.0f, 0.0f), QVector3D(0.0f, 1000.0f, 0.0f)},
        {QVector3D(0.0f, -30.0f, 0.0f), QVector3D(1000.0f, 0.0f, 0.0f)},
    };
    int index = 0;
    for (const auto &c : cases) {
        checkPair("edge", index, c[0], c[1], false, zero, kIdentity);
        checkPair("edge", index, c[0], c[1], true, QVector3D(1.0f, -2.0f, 3.0f), kIdentity);
        index++;
    }
}

} // namespace

int main(int argc, char **argv)
{
    report = argc > 1 && std::strcmp(argv[1], "--report") == 0;

    const int captured =
        checkCapture("host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json")
        + checkCapture("host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-163303.json");
    checkRandom(200000);
    checkEdges();

    std::printf("%d capture samples, %ld comparisons\n", captured, comparisons);
    if (report) {
        for (int i = 0; i < 7; i++) {
            std::printf("max |Qt - core| %-5s %.9g\n", kOutputNames[i], worst[i]);
        }
        return 0;
    }
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("all bit-identical\n");
    return 0;
}
