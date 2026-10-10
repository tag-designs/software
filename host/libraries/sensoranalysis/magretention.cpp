#include "magretention.h"

#include "magquality.h"

#include <algorithm>
#include <cmath>

namespace {

const int kP = MagRetention::kParameters;

/**
 * @brief One row of the design matrix for a normalised sample.
 *
 * @details The same ten-element measurement vector the ten-parameter solver
 *          forms in fUpdateCalibration10EIG(): the six quadratic terms, the
 *          three linear terms and a constant.
 */
void designRow(const QVector3D &v, double row[kP])
{
    const double x = v.x();
    const double y = v.y();
    const double z = v.z();
    row[0] = x * x;
    row[1] = 2.0 * x * y;
    row[2] = 2.0 * x * z;
    row[3] = y * y;
    row[4] = 2.0 * y * z;
    row[5] = z * z;
    row[6] = x;
    row[7] = y;
    row[8] = z;
    row[9] = 1.0;
}

/**
 * @brief Smallest tolerable ratio between the smallest and largest Cholesky
 *        pivot.
 *
 * @details Exact singularity is not what shows up in practice. Samples at one
 *          radius make x^2 + y^2 + z^2 constant, a linear dependency among the
 *          three quadratic columns and the constant column, so D^T D is
 *          singular in exact arithmetic -- but rounding leaves it positive
 *          definite by a hair and the factorisation succeeds with a pivot
 *          ratio around 4e-8, yielding leverages that mean nothing.
 *
 *          Measured ratios: that degenerate case 4e-8; a very quiet capture
 *          with 0.05 uT of scatter 3e-3; the reference fixture 1e-2. Five
 *          orders of magnitude separate the degenerate case from the quietest
 *          real one, so the threshold sits between them with room on both
 *          sides. It errs toward refusing: a refusal costs a fallback, while a
 *          false accept drives evictions from noise.
 */
const double kMinPivotRatio = 1e-5;

/**
 * @brief Cholesky factor of a symmetric positive-definite matrix, in place.
 *
 * @details D^T D is positive definite exactly when the samples determine all
 *          ten parameters, so a failure here is not an error to paper over: it
 *          says the data cannot support the decision this class is being asked
 *          to make. Returns false in that case rather than regularising, which
 *          would answer confidently from nothing.
 */
bool cholesky(double a[kP][kP])
{
    for (int i = 0; i < kP; i++) {
        for (int j = 0; j <= i; j++) {
            double sum = a[i][j];
            for (int k = 0; k < j; k++) {
                sum -= a[i][k] * a[j][k];
            }
            if (i == j) {
                if (sum <= 0.0 || !std::isfinite(sum)) {
                    return false;
                }
                a[i][i] = std::sqrt(sum);
            } else {
                a[i][j] = sum / a[j][j];
            }
        }
        // Keep the strictly upper triangle clear so the factor is unambiguous.
        for (int j = i + 1; j < kP; j++) {
            a[i][j] = 0.0;
        }
    }

    // Succeeding is not the same as being usable; see kMinPivotRatio.
    double lo = a[0][0];
    double hi = a[0][0];
    for (int i = 1; i < kP; i++) {
        lo = std::min(lo, a[i][i]);
        hi = std::max(hi, a[i][i]);
    }
    return hi > 0.0 && lo >= kMinPivotRatio * hi;
}

/**
 * @brief Leverage from a Cholesky factor, without forming an inverse.
 *
 * @details h = d^T (L L^T)^-1 d = |L^-1 d|^2, so one forward substitution and
 *          a dot product. Cheaper than an explicit inverse and better
 *          conditioned.
 */
double leverageFromFactor(const double l[kP][kP], const double d[kP])
{
    double y[kP];
    for (int i = 0; i < kP; i++) {
        double sum = d[i];
        for (int k = 0; k < i; k++) {
            sum -= l[i][k] * y[k];
        }
        y[i] = sum / l[i][i];
    }
    double h = 0.0;
    for (int i = 0; i < kP; i++) {
        h += y[i] * y[i];
    }
    return h;
}

float medianOf(QVector<float> values)
{
    if (values.isEmpty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const int n = static_cast<int>(values.size());
    return (n % 2) ? values.at(n / 2)
                   : 0.5f * (values.at(n / 2 - 1) + values.at(n / 2));
}

} // namespace


MagRetention::MagRetention()
    : MagRetention(Config())
{
}

MagRetention::MagRetention(const Config &config)
    : config_(config)
{
}

QVector<float> MagRetention::leverages(const QVector<QVector3D> &samples) const
{
    const int n = static_cast<int>(samples.size());
    if (n <= kP) {
        // Fewer samples than parameters cannot determine the fit, and with
        // exactly as many every leverage is one.
        return QVector<float>();
    }

    // Work in units of the mean radius. Raw microtesla would put entries of
    // order 2000 beside a constant 1 in the same row, and the resulting
    // condition number is avoidable with one division.
    double radiusSum = 0.0;
    for (int i = 0; i < n; i++) {
        radiusSum += samples.at(i).length();
    }
    const double scale = radiusSum / n;
    if (!(scale > 0.0)) {
        return QVector<float>();
    }

    double m[kP][kP];
    for (int i = 0; i < kP; i++) {
        for (int j = 0; j < kP; j++) {
            m[i][j] = 0.0;
        }
    }

    QVector<double> rows;
    rows.resize(static_cast<qsizetype>(n) * kP);
    for (int s = 0; s < n; s++) {
        double *row = rows.data() + static_cast<qsizetype>(s) * kP;
        designRow(samples.at(s) / static_cast<float>(scale), row);
        for (int i = 0; i < kP; i++) {
            for (int j = 0; j <= i; j++) {
                m[i][j] += row[i] * row[j];
            }
        }
    }
    // Mirror the accumulated lower triangle before factoring.
    for (int i = 0; i < kP; i++) {
        for (int j = i + 1; j < kP; j++) {
            m[i][j] = m[j][i];
        }
    }

    if (!cholesky(m)) {
        return QVector<float>();
    }

    QVector<float> out;
    out.reserve(n);
    for (int s = 0; s < n; s++) {
        const double *row = rows.constData() + static_cast<qsizetype>(s) * kP;
        out.append(static_cast<float>(leverageFromFactor(m, row)));
    }
    return out;
}

MagRetention::Choice MagRetention::choose(const QVector<QVector3D> &samples,
                                          const QVector<bool> &onProbation) const
{
    Choice choice;
    const int n = static_cast<int>(samples.size());
    if (n <= kP) {
        return choice;
    }

    const QVector<float> h = leverages(samples);
    if (h.isEmpty()) {
        return choice;
    }

    // A sample is a candidate unless it is on probation or is the only one
    // holding its patch. The floor is what stops the policy trading coverage
    // away: a lone sample in an otherwise unvisited direction has high leverage
    // and so is safe from the leverage rule, but an outlier test could still
    // take it.
    QVector<bool> candidate(n, true);
    for (int i = 0; i < n && i < static_cast<int>(onProbation.size()); i++) {
        if (onProbation.at(i)) {
            candidate[i] = false;
        }
    }

    if (config_.patchCount > 0) {
        QVector<int> occupancy(config_.patchCount, 0);
        QVector<int> patchOf(n, -1);
        for (int i = 0; i < n; i++) {
            const int p = MagQuality::patch(samples.at(i), config_.patchCount);
            patchOf[i] = p;
            if (p >= 0) {
                occupancy[p]++;
            }
        }
        for (int i = 0; i < n; i++) {
            const int p = patchOf.at(i);
            if (p >= 0 && occupancy.at(p) <= 1) {
                candidate[i] = false;
            }
        }
    }

    // Residuals about the median radius, which is what the magnitude should be
    // once calibrated. The median rather than the mean so that the outliers
    // this is looking for do not set the level they are measured against.
    QVector<float> radii;
    radii.reserve(n);
    for (int i = 0; i < n; i++) {
        radii.append(samples.at(i).length());
    }
    const float centre = medianOf(radii);

    // Scale from the median absolute deviation, not the mean square. A mean
    // square is set by the largest residual, so a single gross outlier inflates
    // the very quantity it is measured against and hides itself -- on a
    // synthetic case here that masking dropped its score below the threshold
    // entirely. The MAD is set by the bulk and leaves the outlier standing
    // out.
    QVector<float> deviations;
    deviations.reserve(n);
    for (int i = 0; i < n; i++) {
        deviations.append(std::fabs(radii.at(i) - centre));
    }
    double variance = 1.4826 * medianOf(deviations);
    variance *= variance;

    if (!(variance > 0.0)) {
        // Every sample at the same radius, which happens with synthetic data
        // and with a buffer holding one repeated reading. Fall back to the
        // mean square so the test still has a scale to work with.
        double residualSq = 0.0;
        for (int i = 0; i < n; i++) {
            const double r = radii.at(i) - centre;
            residualSq += r * r;
        }
        variance = (n > kP) ? residualSq / (n - kP) : 0.0;
    }

    if (config_.rejectOutliers && variance > 0.0) {
        const double scale = std::sqrt(variance);
        int worst = -1;
        double worstScore = 0.0;
        for (int i = 0; i < n; i++) {
            if (!candidate.at(i)) {
                continue;
            }
            // Leverage approaches one for a sample the fit passes exactly
            // through, where the studentized denominator vanishes. Clamp
            // rather than skip: skipping is how a gross outlier -- precisely
            // the case that drives leverage to one -- escaped the test it
            // exists for.
            const double hi = std::min(static_cast<double>(h.at(i)), 0.9999);
            const double r = radii.at(i) - centre;
            const double t = std::fabs(r) / (scale * std::sqrt(1.0 - hi));
            if (t > worstScore) {
                worstScore = t;
                worst = i;
            }
        }
        if (worst >= 0 && worstScore > config_.outlierSigma) {
            choice.index = worst;
            choice.reason = Reason::Outlier;
            choice.score = static_cast<float>(worstScore);
            return choice;
        }
    }

    int least = -1;
    float leastScore = 0.0f;
    for (int i = 0; i < n; i++) {
        if (!candidate.at(i)) {
            continue;
        }
        if (least < 0 || h.at(i) < leastScore) {
            leastScore = h.at(i);
            least = i;
        }
    }
    if (least < 0) {
        return choice;
    }

    choice.index = least;
    choice.reason = Reason::Leverage;
    choice.score = leastScore;
    return choice;
}
