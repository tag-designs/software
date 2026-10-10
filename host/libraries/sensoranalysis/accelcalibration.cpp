#include "accelcalibration.h"

#include "directions.h"

#include <algorithm>
#include <cmath>

AccelCalibration::AccelCalibration()
    : AccelCalibration(Config())
{
}

AccelCalibration::AccelCalibration(const Config &config)
    : config_(config), offered_(0), gated_(0)
{
    if (config_.patches < 1) {
        config_.patches = 1;
    }
    if (config_.perPatch < 1) {
        config_.perPatch = 1;
    }
    if (config_.minimumPatches < 1) {
        config_.minimumPatches = 1;
    }
    if (config_.minimumPatches > config_.patches) {
        config_.minimumPatches = config_.patches;
    }
    reset();
}

void AccelCalibration::reset()
{
    slots_.fill(QVector3D(), config_.patches * config_.perPatch);
    fill_.fill(0, config_.patches);
    next_.fill(0, config_.patches);
    offered_ = 0;
    gated_ = 0;
}

bool AccelCalibration::add(const QVector3D &accel, float oneG)
{
    if (!Directions::finite(accel) || !(oneG > 0.0f)) {
        return false;
    }
    offered_++;

    // Motion, not gravity. Judged on the raw magnitude because no offset
    // estimate is assumed to exist yet; the solver re-gates on the corrected
    // magnitude once one does.
    const float magnitude = accel.length();
    if (std::fabs(magnitude - oneG) > config_.gate * oneG) {
        gated_++;
        return false;
    }

    const int p = Directions::patch(accel, config_.patches);
    if (p < 0) {
        gated_++;
        return false;
    }

    const int base = p * config_.perPatch;
    if (fill_.at(p) < config_.perPatch) {
        slots_[base + fill_.at(p)] = accel;
        fill_[p] = fill_.at(p) + 1;
    } else {
        // Patch full: overwrite its oldest. Selecting which to keep on any
        // property of the reading itself -- closest to one g, say -- would
        // select on the residual and bias the radius, so the rule is
        // deliberately blind to the values.
        slots_[base + next_.at(p)] = accel;
        next_[p] = (next_.at(p) + 1) % config_.perPatch;
    }
    return true;
}

AccelCalibration::Result AccelCalibration::result(float oneG) const
{
    Result r;
    r.patches = config_.patches;
    r.offered = offered_;
    r.gated = gated_;

    double moment[3][3] = {{0.0, 0.0, 0.0},
                           {0.0, 0.0, 0.0},
                           {0.0, 0.0, 0.0}};
    for (int p = 0; p < config_.patches; p++) {
        const int live = fill_.at(p);
        if (live > 0) {
            r.patchesSeen++;
        }
        for (int k = 0; k < live; k++) {
            const QVector3D &v = slots_.at(p * config_.perPatch + k);
            Directions::accumulate(moment, v);
            r.samples++;
        }
    }
    r.coverage = config_.patches > 0
        ? static_cast<float>(r.patchesSeen) / static_cast<float>(config_.patches)
        : 0.0f;
    if (r.samples > 0) {
        double normalised[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                normalised[i][j] = moment[i][j] / r.samples;
            }
        }
        r.isotropy = Directions::isotropy(normalised);
    }

    // Coverage preconditions before the arithmetic, so a fit that comes back
    // valid is one the geometry supports and not merely one the normal
    // equations happened to solve.
    if (r.patchesSeen < config_.minimumPatches
        || r.isotropy < config_.minimumIsotropy) {
        return r;
    }

    GravityFit::Config fitConfig;
    fitConfig.gate = config_.gate;
    fitConfig.refinedGate = config_.refinedGate;
    GravityFit fit(fitConfig);

    for (int p = 0; p < config_.patches; p++) {
        for (int k = 0; k < fill_.at(p); k++) {
            fit.add(slots_.at(p * config_.perPatch + k), oneG);
        }
    }
    const GravityFit::Result first = fit.result(oneG);
    if (!first.valid) {
        return r;
    }

    fit.refit(first.offset);
    for (int p = 0; p < config_.patches; p++) {
        for (int k = 0; k < fill_.at(p); k++) {
            fit.add(slots_.at(p * config_.perPatch + k), oneG);
        }
    }
    const GravityFit::Result second = fit.result(oneG);
    const GravityFit::Result &chosen = second.valid ? second : first;

    r.valid = true;
    r.offset = chosen.offset;
    r.radius = chosen.radius;
    r.residual = chosen.residual;
    return r;
}
