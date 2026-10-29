/**
 * @file    sensorviz_c.cc
 * @brief   The svz_* functions: sensorviz::Session behind a C interface.
 *
 * @details Each call converts at the boundary and nothing else: the session
 *          does the work. Descriptions are written as JSON by a small writer
 *          here (strings escaped per RFC 8259, numbers with 17 significant
 *          digits so they read back exactly, NaN and infinities as null). No
 *          C++ exception crosses the boundary: each call that can throw is
 *          guarded and reports the exception through svz_last_error().
 */
#include "sensorviz_c.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "session.h"
#include "state_dump.h"

using sensorviz::ViewModel;

/// One opened log, and the text last handed out.
struct svz_session {
    sensorviz::Session session; ///< The log, view and transforms.
    std::string text;           ///< The last string returned; valid until the next call.
    std::string error;          ///< The last error.
    ViewModel::Changes changes = ViewModel::None; ///< Changes collected during the current call.

    svz_session()
    {
        // Every change, whichever path makes it (a setter, or a transform
        // remaking its outputs), is reported through the listener.
        session.view().setListener([this](ViewModel::Changes c) { changes |= c; });
    }
};

/// The per-tag preferences, and the text last handed out.
struct svz_preferences {
    sensorviz::PreferenceStore store; ///< The preferences.
    std::string text;                 ///< The last file text returned.
    std::string error;                ///< Why the last load failed.
};

namespace {

/**
 * @class   Json
 * @brief   Writes JSON text, keeping track of where commas go.
 */
class Json
{
public:
    /// Opens an object.
    Json &beginObject() { return open('{'); }
    /// Closes an object.
    Json &endObject() { return close('}'); }
    /// Opens an array.
    Json &beginArray() { return open('['); }
    /// Closes an array.
    Json &endArray() { return close(']'); }

    /// Writes an object key; the value follows.
    Json &key(const std::string &name)
    {
        separate();
        writeString(name);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }

    /// Writes a string value.
    Json &value(const std::string &text)
    {
        separate();
        writeString(text);
        return *this;
    }
    /// Writes a string value.
    Json &value(const char *text) { return value(std::string(text)); }
    /// Writes a number, exactly; NaN and infinities as null.
    Json &value(double number)
    {
        separate();
        if (std::isfinite(number)) {
            char text[32];
            std::snprintf(text, sizeof text, "%.17g", number);
            out_ += text;
        } else {
            out_ += "null";
        }
        return *this;
    }
    /// Writes an integer.
    Json &value(long long number)
    {
        separate();
        out_ += std::to_string(number);
        return *this;
    }
    /// Writes an integer.
    Json &value(int number) { return value(static_cast<long long>(number)); }
    /// Writes an unsigned integer.
    Json &value(unsigned long long number)
    {
        separate();
        out_ += std::to_string(number);
        return *this;
    }
    /// Writes a boolean.
    Json &value(bool flag)
    {
        separate();
        out_ += flag ? "true" : "false";
        return *this;
    }
    /// Writes null.
    Json &null()
    {
        separate();
        out_ += "null";
        return *this;
    }

    /// The text written.
    std::string take() { return std::move(out_); }

private:
    /// Opens a container with @p bracket.
    Json &open(char bracket)
    {
        separate();
        out_ += bracket;
        first_.push_back(true);
        return *this;
    }
    /// Closes a container with @p bracket.
    Json &close(char bracket)
    {
        out_ += bracket;
        first_.pop_back();
        return *this;
    }
    /// Writes the comma before a value or key, unless it follows a key or opens its container.
    void separate()
    {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back()) {
                out_ += ',';
            }
            first_.back() = false;
        }
    }
    /// Writes @p text as a JSON string.
    void writeString(const std::string &text)
    {
        out_ += '"';
        for (unsigned char c : text) {
            switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\b': out_ += "\\b"; break;
            case '\f': out_ += "\\f"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                    out_ += escaped;
                } else {
                    out_ += static_cast<char>(c);
                }
            }
        }
        out_ += '"';
    }

    std::string out_;          ///< The text so far.
    std::vector<bool> first_;  ///< Per open container: nothing written in it yet.
    bool afterKey_ = false;    ///< A key was just written.
};

/// A color as `#rrggbb`.
std::string hexColor(sensorviz::Rgb color)
{
    return sensorviz::colorName(color);
}

/// A time domain's name.
const char *domainName(sensordata::TimeDomain domain)
{
    return domain == sensordata::TimeDomain::ElapsedSeconds ? "elapsed" : "epoch";
}

/// An axis side's name.
const char *sideName(sensorviz::AxisSide side)
{
    return side == sensorviz::AxisSide::Right ? "right" : "left";
}

/// A range as `[lower, upper]`.
void range(Json &json, double lower, double upper)
{
    json.beginArray().value(lower).value(upper).endArray();
}

/// Runs @p body with @p s's changes collected; returns them. Exceptions are kept as errors.
template <typename Body>
uint32_t collecting(svz_session *s, Body body)
{
    if (s == nullptr) {
        return 0;
    }
    s->changes = ViewModel::None;
    try {
        body();
    } catch (const std::exception &e) {
        s->error = std::string("C++ exception: ") + e.what();
    } catch (...) {
        s->error = "C++ exception";
    }
    return s->changes;
}

/// Stores @p text as @p s's returned string, built by @p body; "" on an exception.
template <typename Body>
const char *text(svz_session *s, Body body)
{
    if (s == nullptr) {
        return "";
    }
    try {
        s->text = body();
    } catch (const std::exception &e) {
        s->error = std::string("C++ exception: ") + e.what();
        s->text.clear();
    } catch (...) {
        s->error = "C++ exception";
        s->text.clear();
    }
    return s->text.c_str();
}

/// The C strings @p ids as std::strings.
std::vector<std::string> strings(const char *const *ids, size_t count)
{
    std::vector<std::string> out;
    for (size_t i = 0; ids && i < count; i++) {
        out.emplace_back(ids[i] ? ids[i] : "");
    }
    return out;
}

} // namespace

extern "C" {

uint32_t svz_abi_version(void)
{
    return SVZ_ABI_VERSION;
}

svz_preferences *svz_preferences_new(void)
{
    try {
        return new svz_preferences();
    } catch (...) {
        return nullptr;
    }
}

void svz_preferences_free(svz_preferences *p)
{
    delete p;
}

int svz_preferences_load(svz_preferences *p, const char *json, size_t len, const char *source)
{
    if (p == nullptr) {
        return 0;
    }
    try {
        p->error.clear();
        const std::string text = json ? std::string(json, len) : std::string();
        return p->store.load(text, source ? source : "", p->error) ? 1 : 0;
    } catch (const std::exception &e) {
        p->error = std::string("C++ exception: ") + e.what();
    } catch (...) {
        p->error = "C++ exception";
    }
    return 0;
}

const char *svz_preferences_save(svz_preferences *p)
{
    if (p == nullptr) {
        return "";
    }
    try {
        p->text = p->store.save();
    } catch (...) {
        p->text.clear();
    }
    return p->text.c_str();
}

const char *svz_preferences_last_error(const svz_preferences *p)
{
    return p ? p->error.c_str() : "";
}

svz_session *svz_session_new(void)
{
    try {
        return new svz_session();
    } catch (...) {
        return nullptr;
    }
}

void svz_session_free(svz_session *s)
{
    delete s;
}

const char *svz_last_error(const svz_session *s)
{
    return s ? s->error.c_str() : "";
}

int svz_open(svz_session *s, const char *path, const svz_preferences *p)
{
    if (s == nullptr) {
        return 0;
    }
    int ok = 0;
    collecting(s, [&] {
        s->error.clear();
        std::string error;
        ok = s->session.open(path ? path : "", p ? &p->store : nullptr, error) ? 1 : 0;
        if (!ok) {
            s->error = error;
        }
    });
    return ok;
}

uint32_t svz_apply_preferences(svz_session *s, const svz_preferences *p)
{
    return collecting(s, [&] {
        s->session.applyPreferences(p ? p->store : sensorviz::PreferenceStore());
    });
}

void svz_remember_preferences(const svz_session *s, svz_preferences *p)
{
    if (s && p) {
        try {
            s->session.rememberPreferences(p->store);
        } catch (...) {
        }
    }
}

void svz_forget_preferences(const svz_session *s, svz_preferences *p)
{
    if (s && p) {
        p->store.forget(s->session.preferenceKey());
    }
}

const char *svz_catalog_json(svz_session *s)
{
    return text(s, [&] {
        const sensorviz::StreamCatalog &catalog = s->session.catalog();
        Json json;
        json.beginObject();
        json.key("path").value(catalog.path);
        json.key("tagType").value(catalog.tagType);
        json.key("profileName").value(catalog.profileName);
        json.key("preferenceKey").value(s->session.preferenceKey());
        json.key("info").beginObject();
        for (const auto &[field, value] : catalog.info) {
            json.key(field).value(value);
        }
        json.endObject();
        json.key("timeDomain").value(domainName(catalog.timeDomain));
        json.key("collectionStartEpochMs");
        if (catalog.hasCollectionStart) {
            json.value(static_cast<long long>(catalog.collectionStartEpochMs));
        } else {
            json.null();
        }
        json.key("recordSets").beginArray();
        for (const sensorviz::RecordSet &set : catalog.recordSets) {
            json.beginObject();
            json.key("id").value(set.id);
            json.key("label").value(set.label);
            json.key("table").value(set.table);
            json.key("timeDomain").value(domainName(set.timeDomain));
            json.key("columns").beginArray();
            for (const std::string &column : set.columns) {
                json.value(column);
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();
        json.key("eventMarkers").beginArray();
        for (const sensorviz::EventMarker &marker : catalog.eventMarkers) {
            json.beginObject();
            json.key("id").value(marker.id);
            json.key("label").value(marker.label);
            json.key("timeDomain").value(domainName(marker.timeDomain));
            json.key("times").beginArray();
            for (double time : marker.time) {
                json.value(time);
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();
        json.key("compassCalibration").beginObject();
        json.key("present").value(catalog.hasCompassCalibration);
        json.key("epoch").value(static_cast<long long>(catalog.compassCalibrationEpoch));
        json.key("warning").value(catalog.compassCalibrationWarning);
        const compasscore::CalibrationConstants &c = catalog.compassCalibration;
        json.key("hardIron").beginArray().value(double(c.hardIron.x)).value(double(c.hardIron.y))
            .value(double(c.hardIron.z)).endArray();
        json.key("softIron").beginArray();
        for (const auto &row : c.softIron) {
            for (double element : row) {
                json.value(element);
            }
        }
        json.endArray();
        json.key("accelOffset").beginArray().value(double(c.accelOffset.x))
            .value(double(c.accelOffset.y)).value(double(c.accelOffset.z)).endArray();
        json.endObject();
        const sensorviz::CompassTransform *compass = s->session.transforms().compass();
        json.key("compassSource").value(compass ? compass->source() : std::string());
        json.key("transformFailures").beginArray();
        for (const auto &[id, message] : s->session.transformFailures()) {
            json.beginObject().key("transform").value(id).key("error").value(message).endObject();
        }
        json.endArray();
        json.endObject();
        return json.take();
    });
}

const char *svz_view_json(svz_session *s)
{
    return text(s, [&] {
        const ViewModel &view = s->session.view();
        Json json;
        json.beginObject();
        json.key("streams").beginArray();
        for (const sensorviz::StreamMeta &stream : view.streams()) {
            json.beginObject();
            json.key("id").value(stream.id);
            json.key("label").value(stream.label);
            json.key("units").value(stream.units);
            json.key("group").value(stream.group);
            json.key("groupLabel").value(stream.groupLabel);
            json.key("color").value(hexColor(stream.color));
            json.key("effectiveColor").value(hexColor(view.effectiveColor(stream.id)));
            json.key("derived").value(stream.derived);
            json.key("defaultVisible").value(stream.defaultVisible);
            json.key("axisSide").value(sideName(stream.axisSide));
            json.key("effectiveAxisSide").value(sideName(view.effectiveAxisSide(stream.id)));
            json.key("axisRange");
            if (stream.axisRange.enabled) {
                range(json, stream.axisRange.lower, stream.axisRange.upper);
            } else {
                json.null();
            }
            json.key("timeDomain").value(domainName(stream.timeDomain));
            json.key("stream").value(static_cast<unsigned long long>(stream.dataId));
            json.key("visible").value(view.isVisible(stream.id));
            json.endObject();
        }
        json.endArray();
        json.key("visibleStreamIds").beginArray();
        for (const std::string &id : view.visibleStreamIds()) {
            json.value(id);
        }
        json.endArray();
        json.key("customRanges").beginObject();
        for (const auto &[id, r] : view.customRanges()) {
            json.key(id).beginObject();
            json.key("lower").value(r.lower).key("upper").value(r.upper);
            json.key("explicit").value(view.isExplicitRange(id));
            json.endObject();
        }
        json.endObject();
        json.key("title").value(view.title());
        json.key("titleVisible").value(view.titleVisible());
        json.key("utcOffsetHours").value(view.utcOffsetHours());
        json.key("cursorsVisible").value(view.cursorsVisible());
        json.key("breakAtGaps").value(view.breakAtGaps());
        json.key("parameters").beginObject();
        for (const char *id : {ViewModel::kSeaLevelPressure, ViewModel::kDeclination,
                               ViewModel::kBatteryForward, ViewModel::kActivityTimeConstant}) {
            json.key(id).value(view.parameter(id));
        }
        json.endObject();
        json.endObject();
        return json.take();
    });
}

const char *svz_spec_json(svz_session *s)
{
    return text(s, [&] {
        const sensorviz::PlotSpec spec = s->session.plotSpec();
        Json json;
        json.beginObject();
        json.key("timeDomain").value(domainName(spec.timeDomain));
        json.key("utcOffsetHours").value(spec.utcOffsetHours);
        json.key("xDataRange");
        if (spec.hasXDataRange) {
            range(json, spec.xDataRange.lower, spec.xDataRange.upper);
        } else {
            json.null();
        }
        json.key("traces").beginArray();
        for (const sensorviz::PlotTraceSpec &trace : spec.traces) {
            json.beginObject();
            json.key("id").value(trace.id);
            json.key("name").value(trace.name);
            json.key("stream").value(static_cast<unsigned long long>(trace.stream));
            json.key("color").value(hexColor(trace.color));
            json.key("penWidth").value(trace.penWidth);
            json.key("axis").value(trace.axis);
            json.endObject();
        }
        json.endArray();
        json.key("axes").beginArray();
        for (const sensorviz::PlotAxisSpec &axis : spec.axes) {
            json.beginObject();
            json.key("side").value(sideName(axis.side));
            json.key("label").value(axis.label);
            json.key("color").value(hexColor(axis.color));
            json.key("mode").value(axis.mode == sensorviz::PlotAxisRangeMode::Fixed ? "fixed"
                                   : axis.mode == sensorviz::PlotAxisRangeMode::Linked ? "linked"
                                                                                       : "auto");
            json.key("linkedGroup").value(axis.linkedGroup);
            json.key("traces").beginArray();
            for (int trace : axis.traces) {
                json.value(trace);
            }
            json.endArray();
            json.key("range");
            if (axis.hasRange) {
                range(json, axis.lower, axis.upper);
            } else {
                json.null();
            }
            json.endObject();
        }
        json.endArray();
        json.key("eventMarkerTimes").beginArray();
        for (double time : spec.eventMarkerTimes) {
            json.value(time);
        }
        json.endArray();
        json.key("metadataRows").beginArray();
        for (const std::string &row : spec.metadataRows) {
            json.value(row);
        }
        json.endArray();
        json.key("title").value(spec.title);
        json.key("breakAtGaps").value(spec.breakAtGaps);
        json.endObject();
        return json.take();
    });
}

const char *svz_parameters_json(svz_session *s)
{
    return text(s, [&] {
        const auto specs = s->session.transforms().parameters(s->session.catalog(), s->session.view());
        Json json;
        json.beginArray();
        for (const sensorviz::ParameterSpec &spec : specs) {
            json.beginObject();
            json.key("id").value(spec.id);
            json.key("title").value(spec.title);
            json.key("prompt").value(spec.prompt);
            json.key("units").value(spec.units);
            const bool toggle = spec.kind == sensorviz::ParameterSpec::Kind::Toggle;
            json.key("kind").value(toggle ? "toggle" : "number");
            json.key("minimum").value(spec.minimum);
            json.key("maximum").value(spec.maximum);
            json.key("decimals").value(spec.decimals);
            json.key("value").value(s->session.view().parameter(spec.id));
            json.endObject();
        }
        json.endArray();
        return json.take();
    });
}

const char *svz_hover_json(svz_session *s, double time)
{
    return text(s, [&] {
        Json json;
        json.beginArray();
        for (const std::string &row : s->session.hover(time)) {
            json.value(row);
        }
        json.endArray();
        return json.take();
    });
}

const char *svz_format_time(svz_session *s, double time)
{
    return text(s, [&] { return s->session.formatTime(time); });
}

int svz_range_for_dialog(const svz_session *s, const char *id, double *lower, double *upper)
{
    if (s == nullptr || id == nullptr || !s->session.view().hasStream(id)) {
        return 0;
    }
    const sensorviz::PlotRange r = s->session.rangeForDialog(id);
    if (lower) {
        *lower = r.lower;
    }
    if (upper) {
        *upper = r.upper;
    }
    return 1;
}

size_t svz_envelope(svz_session *s, uint64_t stream, double t0, double t1, int columns,
                    int break_gaps, double *time, float *value, uint64_t *index, size_t capacity)
{
    if (s == nullptr || !s->session.catalog().data) {
        return 0;
    }
    try {
        const sensordata::Envelope envelope = s->session.catalog().data->view(
            static_cast<sensordata::StreamId>(stream), t0, t1, columns,
            break_gaps ? sensordata::GapMode::Break : sensordata::GapMode::Join);
        const size_t count = envelope.time.size();
        if (count > capacity || time == nullptr || value == nullptr || index == nullptr) {
            return count;
        }
        for (size_t i = 0; i < count; i++) {
            time[i] = envelope.time[i];
            value[i] = envelope.value[i];
            index[i] = envelope.index[i] == sensordata::kGapMarker ? SVZ_GAP_MARKER
                                                                    : static_cast<uint64_t>(envelope.index[i]);
        }
        return count;
    } catch (const std::exception &e) {
        s->error = std::string("C++ exception: ") + e.what();
    } catch (...) {
        s->error = "C++ exception";
    }
    return 0;
}

size_t svz_compass_samples(svz_session *s, double *time, float *values, size_t capacity)
{
    if (s == nullptr) {
        return 0;
    }
    const std::vector<sensorviz::OrientationSample> &samples = s->session.compassSamples();
    if (samples.size() > capacity || time == nullptr || values == nullptr) {
        return samples.size();
    }
    for (size_t i = 0; i < samples.size(); i++) {
        const sensorviz::OrientationSample &sample = samples[i];
        time[i] = sample.time;
        float *v = values + 7 * i;
        v[0] = sample.q.w;
        v[1] = sample.q.x;
        v[2] = sample.q.y;
        v[3] = sample.q.z;
        v[4] = sample.dip;
        v[5] = sample.field;
        v[6] = sample.mg;
    }
    return samples.size();
}

const char *svz_state_dump(svz_session *s)
{
    return text(s, [&] {
        sensorviz::SessionDumpDriver driver(s->session);
        return sensorviz::runStateDump(s->session, driver);
    });
}

uint32_t svz_set_stream_visible(svz_session *s, const char *id, int visible)
{
    return collecting(s, [&] { s->session.view().setStreamVisible(id ? id : "", visible != 0); });
}

uint32_t svz_set_visible_streams(svz_session *s, const char *const *ids, size_t count)
{
    return collecting(s, [&] {
        const std::vector<std::string> list = strings(ids, count);
        s->session.view().setVisibleStreams(std::set<std::string>(list.begin(), list.end()));
    });
}

uint32_t svz_set_stream_range(svz_session *s, const char *id, double lower, double upper)
{
    return collecting(s, [&] { s->session.setStreamRange(id ? id : "", lower, upper); });
}

uint32_t svz_reset_zoom(svz_session *s)
{
    return collecting(s, [&] { s->session.resetZoom(); });
}

uint32_t svz_set_colors(svz_session *s, const char *const *ids, const uint32_t *rgb, size_t count)
{
    return collecting(s, [&] {
        std::map<std::string, sensorviz::Rgb> colors;
        const std::vector<std::string> list = strings(ids, count);
        for (size_t i = 0; rgb && i < list.size(); i++) {
            colors[list[i]] = sensorviz::Rgb{static_cast<uint8_t>(rgb[i] >> 16),
                                             static_cast<uint8_t>(rgb[i] >> 8),
                                             static_cast<uint8_t>(rgb[i])};
        }
        s->session.view().setColorOverrides(colors);
    });
}

uint32_t svz_set_axis_sides(svz_session *s, const char *const *ids, const int *right, size_t count)
{
    return collecting(s, [&] {
        std::map<std::string, sensorviz::AxisSide> sides;
        const std::vector<std::string> list = strings(ids, count);
        for (size_t i = 0; right && i < list.size(); i++) {
            sides[list[i]] = right[i] ? sensorviz::AxisSide::Right : sensorviz::AxisSide::Left;
        }
        s->session.view().setAxisSideOverrides(sides);
    });
}

uint32_t svz_set_title(svz_session *s, const char *title)
{
    return collecting(s, [&] { s->session.view().setTitle(title ? title : ""); });
}

uint32_t svz_set_title_visible(svz_session *s, int visible)
{
    return collecting(s, [&] { s->session.view().setTitleVisible(visible != 0); });
}

uint32_t svz_set_utc_offset(svz_session *s, int hours)
{
    return collecting(s, [&] { s->session.view().setUtcOffsetHours(hours); });
}

uint32_t svz_set_cursors_visible(svz_session *s, int visible)
{
    return collecting(s, [&] { s->session.view().setCursorsVisible(visible != 0); });
}

uint32_t svz_set_break_at_gaps(svz_session *s, int break_gaps)
{
    return collecting(s, [&] { s->session.view().setBreakAtGaps(break_gaps != 0); });
}

int svz_set_parameter(svz_session *s, const char *id, double value, uint32_t *changes)
{
    if (s == nullptr) {
        return 0;
    }
    bool ok = false;
    const uint32_t made = collecting(s, [&] {
        std::string error;
        ok = s->session.setParameter(id ? id : "", value, error);
        if (!ok) {
            s->error = error;
        }
    });
    if (changes) {
        *changes = made;
    }
    return ok ? 1 : 0;
}

} // extern "C"
