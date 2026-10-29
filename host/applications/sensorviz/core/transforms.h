/**
 * @file    transforms.h
 * @brief   Qt-free display transforms: the streams sensorViz derives from a
 *          log -- altitude, IMU vector magnitudes and compass orientation --
 *          their parameters as data, and the registry that runs them.
 *
 * @details Part of `sensorviz_core`. A Transform reads its inputs from the
 *          StreamCatalog and the log's sensordata::Log, registers each output's
 *          samples with the Log, and hands the output's StreamMeta to a
 *          TransformSink, which the front end implements: the Qt app adds it
 *          to its ViewModel and redraws, once per output, as it always has.
 *          Results are never written to the log.
 *
 *          Inputs are read two ways. Altitude and compass orientation go
 *          through `forEachRow()`/SQLite doubles, so they are exactly what
 *          they always were; the magnitudes use the Log's stored floats.
 *          Compass orientation comes from a processed log's stored
 *          quaternions, or from CompassTag's `compass_raw` record set with the
 *          log's calibration, through sensoranalysis's core
 *          (compasscore::solveOrientation(), compasscore::displayAngles()).
 *          IMUTag's ImuAccel and ImuMag are never paired.
 *
 *          The activity filter is not here: it stays in the Qt sensorViz until
 *          that app retires.
 *
 * @see     host/applications/sensorviz/design/proposals/toolkit-neutral-core.md
 */
#ifndef SENSORVIZ_CORE_TRANSFORMS_H
#define SENSORVIZ_CORE_TRANSFORMS_H

#include <memory>
#include <string>
#include <vector>

#include "compass_core.h"
#include "stream_catalog.h"
#include "stream_meta.h"
#include "view_model.h"

namespace sensorviz {

/**
 * @struct  ParameterSpec
 * @brief   One transform parameter, described so a front end can build its control.
 *
 * @details The value lives in the ViewModel under #id
 *          (ViewModel::parameter(), ViewModel::setParameter()); a toggle is 1
 *          or 0. Change it through TransformRegistry::setParameter(), which
 *          remakes the affected outputs.
 */
struct ParameterSpec
{
    /// Kind of control.
    enum class Kind
    {
        Number, ///< A number between #minimum and #maximum, shown with #decimals.
        Toggle, ///< On or off.
    };

    std::string id;          ///< ViewModel parameter id, e.g. ViewModel::kSeaLevelPressure.
    std::string title;       ///< Dialog title, e.g. "Sea-level Pressure".
    std::string prompt;      ///< Field label, e.g. "Mean sea-level pressure (mbar)".
    std::string units;       ///< Units, e.g. "mbar"; may be empty.
    Kind kind = Kind::Number; ///< Kind of control.
    double minimum = 0.0;    ///< Smallest value accepted (Number only).
    double maximum = 0.0;    ///< Largest value accepted (Number only).
    int decimals = 0;        ///< Decimals shown and entered (Number only).
};

/**
 * @struct  OrientationSample
 * @brief   One compass orientation, as the compass panel shows it.
 */
struct OrientationSample
{
    double time = 0.0;      ///< Row time, seconds in the log's time domain.
    sensormath::Quatf q;    ///< Magnetic-frame orientation; identity when no solution exists.
    float dip = 0.0f;       ///< Magnetic dip, degrees.
    float field = 0.0f;     ///< Magnetic field magnitude, uT.
    float mg = 0.0f;        ///< Acceleration magnitude, mg.
};

/**
 * @class   TransformSink
 * @brief   Where transforms put their outputs: implemented by the front end.
 */
class TransformSink
{
public:
    virtual ~TransformSink() = default;

    /**
     * @brief   Adds a transform output, or replaces the stream with the same id.
     * @param[in] stream   The output; its StreamMeta::dataId is already
     *                     registered with the log. The replaced stream's data
     *                     is the sink's to release.
     * @param[in] visible  Whether to show it (ignored for derived streams).
     */
    virtual void publish(const StreamMeta &stream, bool visible) = 0;

    /// Reports a problem that does not stop the transform, for a log.
    virtual void warn(const std::string &message) = 0;
};

/**
 * @class   TransformContext
 * @brief   What a transform may use: the catalog, the view state, the log and the sink.
 */
class TransformContext
{
public:
    /// A context over one opened log. All three must outlive it.
    TransformContext(const StreamCatalog &catalog, ViewModel &view, TransformSink &sink)
        : catalog_(catalog), view_(view), sink_(sink)
    {
    }

    /// The opened log.
    const StreamCatalog &catalog() const { return catalog_; }
    /// The view state: current streams, visibility, ranges and parameters.
    ViewModel &view() { return view_; }
    /// The view state, read-only.
    const ViewModel &view() const { return view_; }
    /// The output sink.
    TransformSink &sink() { return sink_; }

    /**
     * @brief   Whether a remade output should be shown: as it is now, or shown
     *          when it does not exist yet or is derived.
     * @param[in] id  Output stream id.
     */
    bool visibleOnRemake(const std::string &id) const;

    /**
     * @brief   Reads a stored stream's exact SQLite doubles, in time order.
     * @param[in]  stream  A stored stream of the log.
     * @param[out] time    Sample times, seconds.
     * @param[out] value   Sample values.
     * @return  false for a derived stream or a failed read, which is also
     *          reported to the sink.
     */
    bool readDoubles(const StreamMeta &stream, std::vector<double> &time,
                     std::vector<double> &value) const;

private:
    const StreamCatalog &catalog_; ///< The opened log.
    ViewModel &view_;              ///< The view state.
    TransformSink &sink_;          ///< Where outputs go.
};

/**
 * @brief   The sensordata description of a transform output, for
 *          sensordata::Log::addDerived() and addComputed().
 * @param[in] stream  The output; its id, label, units and time domain are used.
 */
sensordata::StreamInfo derivedStreamInfo(const StreamMeta &stream);

/**
 * @brief   Altitude from pressure, by the barometric formula, m.
 * @param[in] pressureMbar  Pressure, mbar.
 * @param[in] seaLevelMbar  Mean sea-level pressure, mbar.
 */
double altitudeFromPressure(double pressureMbar, double seaLevelMbar);

/**
 * @brief   Altitude from pressure and the pressure sensor's temperature, by the
 *          hypsometric formula, m.
 * @param[in] pressureMbar  Pressure, mbar.
 * @param[in] temperatureC  Air temperature at the sensor, degrees C.
 * @param[in] seaLevelMbar  Mean sea-level pressure, mbar.
 */
double altitudeFromPressure(double pressureMbar, double temperatureC, double seaLevelMbar);

/**
 * @class   Transform
 * @brief   One display transform: its applicability, parameters and outputs.
 */
class Transform
{
public:
    virtual ~Transform() = default;

    /// Stable id, e.g. `altitude`.
    virtual const char *id() const = 0;

    /// Whether its inputs are present.
    virtual bool applicable(const StreamCatalog &catalog, const ViewModel &view) const = 0;

    /// Its parameters, in menu order; empty when it has none.
    virtual std::vector<ParameterSpec> parameters() const { return {}; }

    /**
     * @brief   Makes every output, replacing earlier ones.
     * @param[in,out] context  The log, view and sink.
     * @param[out]    error    Why no output was made; set only on failure.
     * @return  false when the inputs are present but unusable.
     */
    virtual bool run(TransformContext &context, std::string &error) = 0;

    /**
     * @brief   Remakes what parameter @p id affects, after it changed.
     * @details Runs the whole transform unless overridden.
     */
    virtual bool update(TransformContext &context, const std::string &id, std::string &error)
    {
        (void)id;
        return run(context, error);
    }

    /// Forgets anything kept from the previous log.
    virtual void reset() {}
};

/**
 * @class   CompassTransform
 * @brief   Heading, acceleration, pitch, roll, dip and field from compass orientation.
 *
 * @details Keeps the orientation samples between runs, for the compass panel
 *          and so that a declination or battery-forward change remakes only
 *          the heading.
 */
class CompassTransform : public Transform
{
public:
    const char *id() const override { return "compass"; }
    bool applicable(const StreamCatalog &catalog, const ViewModel &view) const override;
    std::vector<ParameterSpec> parameters() const override;
    bool run(TransformContext &context, std::string &error) override;
    bool update(TransformContext &context, const std::string &id, std::string &error) override;
    void reset() override;

    /// The orientation samples, in time order; empty before a run or after a failed one.
    const std::vector<OrientationSample> &samples() const { return samples_; }

    /// Where the samples came from, for File Info: "stored quaternions (label)" or
    /// "derived from raw compass samples"; empty when there are none.
    const std::string &source() const { return source_; }

private:
    /// Fills samples_, source_ and timeSource_ from the log.
    bool loadSamples(const TransformContext &context, std::string &error);
    /// Publishes the heading from samples_ with the current parameters.
    void publishHeading(TransformContext &context, bool visible) const;

    std::vector<OrientationSample> samples_;                  ///< The orientation samples.
    std::string source_;                                      ///< Their source, for File Info.
    sensordata::StreamId timeSource_ = sensordata::kNoStream; ///< The stored stream whose times they share.
};

/**
 * @class   TransformRegistry
 * @brief   The transforms sensorViz runs, in the order they run after a load.
 */
class TransformRegistry
{
public:
    /// Altitude, compass orientation and the IMU magnitudes, in that order.
    static TransformRegistry makeDefault();

    /// The transform @p id, or null.
    Transform *find(const std::string &id) const;

    /// The compass transform, for its samples; null if not registered.
    const CompassTransform *compass() const;

    /// Whether transform @p id exists and is applicable.
    bool applicable(const std::string &id, const StreamCatalog &catalog, const ViewModel &view) const;

    /// The spec of parameter @p id, or null.
    const ParameterSpec *parameter(const std::string &id) const;

    /**
     * @brief   The parameters of the applicable transforms, in run order:
     *          what a front end offers for the open log.
     */
    std::vector<ParameterSpec> parameters(const StreamCatalog &catalog, const ViewModel &view) const;

    /**
     * @brief   Runs every applicable transform for a newly opened log, in order.
     * @param[in,out] context  The log, view and sink.
     * @return  (transform id, error) for each transform that failed; the
     *          others ran regardless.
     */
    std::vector<std::pair<std::string, std::string>> runAll(TransformContext &context);

    /**
     * @brief   Sets a parameter and remakes what it affects.
     * @param[in,out] context  The log, view and sink.
     * @param[in]     id       Parameter id.
     * @param[in]     value    New value; a toggle is on when non-zero.
     * @param[out]    error    Why the remake failed; set only on failure.
     * @return  false when the owning transform failed to remake its outputs;
     *          true otherwise, including when it is not applicable.
     */
    bool setParameter(TransformContext &context, const std::string &id, double value,
                      std::string &error);

private:
    std::vector<std::shared_ptr<Transform>> transforms_; ///< In run order.
    std::vector<ParameterSpec> parameters_;              ///< Every transform's parameters, in run order.
};

} // namespace sensorviz

#endif
