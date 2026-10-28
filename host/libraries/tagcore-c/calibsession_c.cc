/**
 * @file    calibsession_c.cc
 * @brief   C interface over calibcore::CalibrationSession; see
 *          calibsession_c.h.
 *
 * @details Converts between C arrays and sensormath types and copies results
 *          out field for field; it computes nothing itself.
 */
#include "calibsession_c.h"

#include "calibration_session.h"
#include "compass_core.h"

#include <new>

using sensormath::Quatf;
using sensormath::Vec3f;

/// The handle: a session, nothing else.
struct tcs_session {
    calibcore::CalibrationSession session; ///< The calibration.
};

namespace {

/// Three floats as a vector.
Vec3f vec(const float v[3])
{
    return Vec3f{v[0], v[1], v[2]};
}

/// A vector into three floats.
void put(Vec3f v, float out[3])
{
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

} // namespace

extern "C" {

unsigned tcs_abi_version(void)
{
    return TCS_ABI_VERSION;
}

tcs_session *tcs_new(void)
{
    return new (std::nothrow) tcs_session();
}

void tcs_free(tcs_session *s)
{
    delete s;
}

void tcs_clear(tcs_session *s)
{
    s->session.clear();
}

int tcs_add_sample(tcs_session *s, const float mag[3], int has_accel, const float accel[3])
{
    const Vec3f a = (has_accel && accel) ? vec(accel) : Vec3f{};
    return s->session.addSample(vec(mag), has_accel != 0 && accel != nullptr, a) ? 1 : 0;
}

int tcs_has_calibration(const tcs_session *s)
{
    return s->session.hasCalibration() ? 1 : 0;
}

void tcs_calibrate(const tcs_session *s, const float mag[3], float out[3])
{
    put(s->session.calibrate(vec(mag)), out);
}

size_t tcs_points(const tcs_session *s, float *xyz, size_t max)
{
    const std::vector<Vec3f> points = s->session.calibratedPoints();
    for (size_t i = 0; i < points.size() && i < max; i++) {
        put(points[i], xyz + 3 * i);
    }
    return points.size();
}

int tcs_constants(const tcs_session *s, float *B, float V[3], float A[9])
{
    float m[3][3];
    const bool valid = s->session.constants(B, V, m);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            A[3 * i + j] = m[i][j];
        }
    }
    return valid ? 1 : 0;
}

void tcs_set_constants(tcs_session *s, const float V[3], const float A[9])
{
    float m[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i][j] = A[3 * i + j];
        }
    }
    s->session.setConstants(V, m);
}

float tcs_field(const tcs_session *s)
{
    return s->session.field();
}

void tcs_set_accel_offset(tcs_session *s, const float offset[3])
{
    s->session.setAccelOffset(vec(offset));
}

void tcs_update_quality(tcs_session *s)
{
    s->session.updateQuality();
}

void tcs_get_quality(const tcs_session *s, tcs_quality *out)
{
    const calibcore::MagQuality::Result &r = s->session.quality();
    out->patches_seen = r.patchesSeen;
    out->mag_patches = r.magPatches;
    out->coverage = r.coverage;
    out->patch_min = r.patchMin;
    out->patch_max = r.patchMax;
    out->isotropy = r.isotropy;
    out->attitude_diversity = r.attitudeDiversity;
    out->attitude_cells = r.attitudeCells;
    out->attitude_patches = r.attitudePatches;
    out->field = r.field;
    out->fit_error = r.fitError;
    out->residual_spread = r.residualSpread;
    out->residual_p95 = r.residualP95;
    out->residual_hard_iron = r.residualHardIron;
    out->dip_mean_deg = r.dipMeanDeg;
    out->dip_spread_deg = r.dipSpreadDeg;
    out->dip_p95_deg = r.dipP95Deg;
    out->dip_samples = r.dipSamples;
    out->accel_samples = r.accelSamples;
    out->have_accel_metrics = r.haveAccelMetrics ? 1 : 0;
    out->samples = r.samples;
}

void tcs_get_accel_fit(const tcs_session *s, tcs_accel_fit *out)
{
    const calibcore::AccelCalibration::Result &r = s->session.accelOffset();
    out->valid = r.valid ? 1 : 0;
    put(r.offset, out->offset);
    out->radius = r.radius;
    out->residual = r.residual;
    out->samples = r.samples;
    out->patches_seen = r.patchesSeen;
    out->patches = r.patches;
    out->coverage = r.coverage;
    out->isotropy = r.isotropy;
    out->offered = r.offered;
    out->gated = r.gated;
}

void tcs_evictions(const tcs_session *s, int out[3])
{
    out[0] = s->session.evictionCount();
    out[1] = s->session.leverageEvictionCount();
    out[2] = s->session.outlierEvictionCount();
}

int tcs_orientation_of(tcs_session *s, const float mag[3], const float accel[3], tcs_orientation *out)
{
    compasscore::Orientation o;
    if (!s->session.orientation(vec(mag), vec(accel), o)) {
        return 0;
    }
    out->q[0] = o.q.w;
    out->q[1] = o.q.x;
    out->q[2] = o.q.y;
    out->q[3] = o.q.z;
    out->dip = o.dip;
    out->field = o.field;
    out->mg = o.mg;
    return 1;
}

void tcs_display_angles(const float q[4], tcs_angles *out)
{
    const compasscore::DisplayAngles a = compasscore::displayAngles(Quatf{q[0], q[1], q[2], q[3]});
    out->yaw = a.yaw;
    out->pitch = a.pitch;
    out->roll = a.roll;
}

void tcs_model_rotation(float pitch, float roll, float yaw, float q[4])
{
    const Quatf r = sensormath::fromAxisAndAngle(Vec3f{0, 0, 1}, yaw)
        * sensormath::fromAxisAndAngle(Vec3f{0, 1, 0}, roll)
        * sensormath::fromAxisAndAngle(Vec3f{1, 0, 0}, pitch);
    q[0] = r.w;
    q[1] = r.x;
    q[2] = r.y;
    q[3] = r.z;
}

} // extern "C"
