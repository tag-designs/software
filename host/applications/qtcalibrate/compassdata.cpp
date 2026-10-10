#include <cmath>
#include <cfloat>
#include "compassdata.h"
#include "compass_processor.h"
#include <iostream>
#include <random>

namespace
{

/// Acceleration magnitude that corresponds to one g in calibration stream
/// units. The stream is milli-g on every current tag. TagInfo carries an
/// accelconstant that should eventually supply this instead of a constant
/// here; the capture file now records it, so the value is at least no longer
/// invisible to anyone reading a fixture.
const float kStreamOneG = 1000.0f;

/// Additions for which a freshly stored sample is exempt from eviction. One
/// solver cycle: MagCal_Run() refits every twentieth sample, so a sample
/// survives long enough to have influenced a fit before it can be judged by
/// one.
const int kProbationAdds = 20;

CompassCalibration calibrationFromMagcal()
{
	// Convert the inherited global magcal representation into the shared
	// sensoranalysis calibration type used by CompassProcessor and log viewers.
	CompassCalibration::Matrix softIron;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			softIron[i][j] = magcal.invW[i][j];
		}
	}

	return CompassCalibration::fromMagnetometerConstants(
		QVector3D(magcal.V[0], magcal.V[1], magcal.V[2]),
		softIron);
}

} // namespace


CompassData::CompassData(QObject *parent) : QObject{parent}
{
    clear();
}

bool CompassData::addData(QVector3D &mag, bool hasAccel,
                          const QVector3D &accel)
{
    // raw_data() updates the inherited solver. When it produces a usable
    // calibration, emit a UI update and return the calibrated sample for
    // plotting.
    bool result = raw_data(mag, hasAccel, accel);
    if (result) {
        emit calibration_update();
    }
    if (magcal.ValidMagCal)
    {
        apply_calibration(mag);
        return true;
    }
    return false;
}

void CompassData::getData(QList<QVector3D> &data)
{
    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
			QVector3D point = BpFast(i);
			apply_calibration(point);
			data.append(point);
        }
    }       
}

bool CompassData::getCalibrationConstants(float *B, float *V, float (*A)[3])
{
    *B = magcal.B;
    for (int i = 0; i < 3; i++){
        V[i] = magcal.V[i];
        for (int j = 0; j < 3; j++){
            A[i][j] = magcal.invW[i][j];
        }
    }
    return (magcal.ValidMagCal);
}

void CompassData::setCalibrationConstants(float *V, float (*A)[3])
{
	 for (int i = 0; i < 3; i++){
        magcal.V[i] = V[i];
        for (int j = 0; j < 3; j++){
            magcal.invW[i][j] = A[i][j] ;
        }
		
    }
	magcal.ValidMagCal = 4;

	// B is not stored on the tag; recover it from the buffer if there is one.
	const CompassCalibration calibration = calibrationFromMagcal();
	double sum = 0.0;
	int count = 0;
	for (int i = 0; i < MAGBUFFSIZE; i++) {
		if (magcal.valid[i]) {
			sum += calibration.apply(BpFast(i)).length();
			count++;
		}
	}
	if (count > 0) {
		magcal.B = sum / count;
	}
}

void CompassData::setAccelOffset(const QVector3D &offset)
{
	gravity = AccelCalibration::Result();
	gravity.valid = true;
	gravity.offset = offset;
	gravityLoaded = true;
}

void CompassData::apply_calibration(QVector3D &mag){
	mag = calibrationFromMagcal().apply(mag);
}


float CompassData::getField()
{
	return magcal.B;
}


bool CompassData::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q, 
                            float& dip, float& field)
{
	float alpha = 0.4; // filter coefficient

	apply_calibration(magin);

	// qtcalibrate filters live vectors before solving orientation. Log viewers
	// use unfiltered samples and call CompassProcessor directly.
	if (gravity.valid) {
		accel -= gravity.offset;
	}
	acc_filt = alpha * acc_filt + (1.0-alpha) * accel;
	mag_filt = alpha * mag_filt + (1.0-alpha) * magin;

	accel = acc_filt;
	magin = mag_filt;

	CompassRawSample raw;
	raw.accel = accel;
	raw.mag = magin;

	// qtcalibrate keeps ownership of live calibration and low-pass filtering.
	// The UI-free eCompass solve itself is shared with sensorviz.
	CompassProcessor processor{CompassCalibration()};
	CompassDerivedSample derived;
	if (!processor.deriveCalibratedSample(raw, derived))
		return false;

	q = derived.q;
	dip = derived.dip;
	field = derived.field;
	return true;
}

// The remaining methods manage the magcal sample buffer. The solver itself
// lives in magcal/.

void CompassData::clear()
{
    raw_data_reset();
}

void CompassData::qualityUpdate(){
    quality.reset();

    // The accelerometer fit comes from its own population, which has been
    // accumulating at intake rather than waiting for this tick.
    // An offset loaded from the tag stands until the live population yields a
    // valid fit of its own.
    const AccelCalibration::Result fit = accelCal.result(kStreamOneG);
    if (fit.valid || !gravityLoaded) {
        gravity = fit;
        gravityLoaded = false;
    }

    // The inclination of a sample is CompassProcessor's to compute: the
    // magnetometer and the accelerometer do not share an axis convention, and
    // that conversion lives in one place. See docs/shared/sensor-axes.md.
    // MagQuality only aggregates what it is handed.
    CompassProcessor processor{CompassCalibration()};

    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
			QVector3D point = BpFast(i);
			apply_calibration(point);
            if (accelBufferValid[i]) {
                CompassRawSample raw;
                // The zero-g offset tilts the apparent gravity by an amount
                // that varies with orientation, so it shows up as dip scatter.
                // Removing it is worth about a third of that spread.
                raw.accel = gravity.valid ? accelBuffer[i] - gravity.offset
                                          : accelBuffer[i];
                raw.mag = point;
                CompassDerivedSample derived;
                if (processor.deriveCalibratedSample(raw, derived)) {
                    quality.add(point, &accelBuffer[i], derived.dip,
                                kStreamOneG);
                } else {
                    quality.add(point);
                }
            } else {
                quality.add(point);
            }
        }
    }
    metrics = quality.result();
}

void CompassData::raw_data_reset(void)
{
	//rawcount = OVERSAMPLE_RATIO;
	//fusion_init();
	memset((void*) &magcal, 0, sizeof(magcal));
	for (int i = 0; i < MAGBUFFSIZE; i++) {
		accelBufferValid[i] = false;
		accelBuffer[i] = QVector3D();
		slotFilledAt[i] = 0;
	}
	addCounter = 0;
	discardRng.seed(kDiscardSeed);
	accelCal.reset();
	gravity = AccelCalibration::Result();
	gravityLoaded = false;
	evictions = 0;
	leverageEvictions = 0;
	outlierEvictions = 0;
	magcal.invW[0][0] = 1.0f;
	magcal.invW[1][1] = 1.0f;
	magcal.invW[2][2] = 1.0f;
	magcal.FitError = 100.0f;
	magcal.FitErrorAge = 100.0f;
	magcal.B = 50.0f;
}

/**
 * @brief Choose a sample to discard using leverage and Cook's distance.
 *
 * @return An index, or -1 when the policy declines and the caller should fall
 *         back to the inherited scan.
 *
 * @details Samples are handed over calibrated. Leverage is unchanged by that:
 *          an affine change of coordinates maps the ten quadratic basis terms
 *          onto linear combinations of themselves, so the design matrix is
 *          reparametrised and leverage is invariant. The residual is not
 *          invariant, and needs the calibrated magnitudes to mean anything,
 *          which is why the outlier rule is only armed once a calibration
 *          exists -- before that, "wrong magnitude" has no definition and the
 *          hard iron offset alone would make honest samples look wild.
 */
int CompassData::chooseDiscardByLeverage()
{
	QVector<QVector3D> samples;
	QVector<bool> probation;
	QVector<int> slotOf;
	samples.reserve(MAGBUFFSIZE);
	probation.reserve(MAGBUFFSIZE);
	slotOf.reserve(MAGBUFFSIZE);

	for (int i = 0; i < MAGBUFFSIZE; i++) {
		if (!magcal.valid[i]) {
			continue;
		}
		QVector3D point = BpFast(i);
		apply_calibration(point);
		samples.append(point);
		probation.append((addCounter - slotFilledAt[i]) < kProbationAdds);
		slotOf.append(i);
	}

	MagRetention::Config cfg = leverageRetention.config();
	cfg.rejectOutliers = (magcal.ValidMagCal != 0);
	const MagRetention::Choice choice =
		MagRetention(cfg).choose(samples, probation);

	if (choice.reason == MagRetention::Reason::None
	    || choice.index < 0 || choice.index >= slotOf.size()) {
		return -1;
	}
	if (choice.reason == MagRetention::Reason::Outlier) {
		outlierEvictions++;
	} else {
		leverageEvictions++;
	}
	return slotOf.at(choice.index);
}

/**
 * @brief Choose a sample to discard when the buffer is full.
 *
 * @return An index, or -1 when the policy declines.
 *
 * @details One rule now. What stood here was MotionCal's: a rate-limited scan
 *          for the point furthest from the mean field strength, gated on
 *          quality.c's gap figure, falling back to an O(N^2) search for the
 *          two closest points with one of them dropped at random. It is gone
 *          with the file it depended on.
 *
 *          A decline is not a failure to handle here. It means too few
 *          samples or a design the data cannot determine, which is exactly
 *          when discarding on leverage would be guesswork; the caller takes a
 *          slot at random, as it did before when both branches declined.
 */
int CompassData::choose_discard_magcal(void)
{
	return chooseDiscardByLeverage();
}

void CompassData::add_magcal_data(const QVector3D &data, bool hasAccel,
                                  const QVector3D &accel)
{
	int i;

	// first look for an unused caldata slot
	for (i=0; i < MAGBUFFSIZE; i++) {
		if (!magcal.valid[i]) break;
	}
	// If the buffer is full, we must choose which old data to discard.
	// We must choose wisely!  Throwing away the wrong data could prevent
	// collecting enough data distributed across the entire 3D angular
	// range, preventing a decent cal from ever happening at all.  Making
	// any assumption about good vs bad data is particularly risky,
	// because being wrong could cause an unstable feedback loop where
	// bad data leads to wrong decisions which leads to even worse data.
	// But if done well, purging bad data has massive potential to
	// improve results.  The trick is telling the good from the bad while
	// still in the process of learning what's good...
	if (i >= MAGBUFFSIZE) {
		i = choose_discard_magcal();
		if (i < 0 || i >= MAGBUFFSIZE) {
			i = static_cast<int>(discardRng() % MAGBUFFSIZE);
		}
		evictions++;
	}
	addCounter++;
	slotFilledAt[i] = addCounter;
	// add it to the cal buffer
	magcal.BpFast[0][i] = data[0];
	magcal.BpFast[1][i] = data[1];
	magcal.BpFast[2][i] = data[2];
	magcal.valid[i] = 1;

	// Whichever slot the discard policy chose, its old accelerometer reading
	// goes with its old magnetometer reading.
	accelBuffer[i] = accel;
	accelBufferValid[i] = hasAccel;
}


bool CompassData::raw_data(const QVector3D &data, bool hasAccel,
                           const QVector3D &accel)
{
	// Two independent intakes. The magnetometer takes every sample; the
	// accelerometer takes the ones that are gravity rather than motion, and
	// keeps them by direction. Neither decides anything for the other.
	if (hasAccel) {
		accelCal.add(accel, kStreamOneG);
	}
	add_magcal_data(data, hasAccel, accel);
	return MagCal_Run(&magcal) != 0;
}



QVector3D CompassData::BpFast(int i){
	if ((i < 0) || (i >= MAGBUFFSIZE)){
		return QVector3D();
	}
	else {
		return QVector3D(magcal.BpFast[0][i], 
                		 magcal.BpFast[1][i],
                		 magcal.BpFast[2][i]);
	}

}
