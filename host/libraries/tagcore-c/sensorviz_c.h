/**
 * @file    sensorviz_c.h
 * @brief   C interface to sensorViz's Qt-free core: open a tag log, read what
 *          the plot shows, change the view, and fetch the plot's envelopes.
 *
 * @details Wraps `sensorviz::Session` (host/applications/sensorviz/core/
 *          session.h) so a program in another language -- the Rust sensorViz
 *          first -- shows a log exactly as the Qt sensorViz does, through the
 *          same code: the log reader, the display transforms, the plot policy,
 *          the hover text and the preference file.
 *
 *          What crosses, and how:
 *
 *          - **Descriptions as JSON.** The catalog, the view state, the
 *            PlotSpec, the transform parameters and the hover rows are JSON
 *            text, UTF-8. The returned pointer belongs to the handle and stays
 *            valid until the next call on it. The schemas are listed in
 *            host/libraries/sensorviz-session-rs/README.md, whose types
 *            mirror them.
 *          - **Samples as arrays.** svz_envelope() copies an M4 envelope into
 *            the caller's time, value and index arrays; svz_compass_samples()
 *            likewise copies the compass orientation samples.
 *          - **Changes as bits.** Each setter returns the SVZ_CHANGE_* bits of
 *            what it changed, zero when nothing did, so the caller redraws
 *            only what it must.
 *
 *          @code
 *          svz_preferences *p = svz_preferences_new();
 *          svz_session *s = svz_session_new();
 *          if (!svz_open(s, path, p)) { report(svz_last_error(s)); }
 *          const char *spec = svz_spec_json(s);       // draw it
 *          // for each trace: svz_envelope(s, trace.stream, t0, t1, width, ...)
 *          svz_set_stream_range(s, "imu_pressure", 950.0, 1010.0);
 *          svz_session_free(s);
 *          svz_preferences_free(p);
 *          @endcode
 *
 *          Built into the same library as taglogwriter_c.h and
 *          calibsession_c.h (libtagcore_c), which exports these functions
 *          beside the tlw_* and tcs_* ones.
 *
 * @note    A handle is not thread-safe: use each from one thread at a time.
 *          Separate handles are independent.
 */
#ifndef SENSORVIZ_C_H
#define SENSORVIZ_C_H

#include <stddef.h>
#include <stdint.h>

#include "taglogwriter_c.h" /* TLW_API */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def     SVZ_ABI_VERSION
 * @brief   Version of this interface; svz_abi_version() returns the value the
 *          library was built with.
 *
 * @details Raised whenever a function's meaning, a signature or a JSON
 *          schema changes incompatibly, so a caller built against one header
 *          refuses a library built from another rather than misbehaving.
 */
#define SVZ_ABI_VERSION 1u

/** @name Change bits returned by the setters (sensorviz::ViewModel::Change) */
/** @{ */
#define SVZ_CHANGE_STREAMS    0x001u /**< Streams were added, replaced or removed. */
#define SVZ_CHANGE_VISIBILITY 0x002u /**< Which streams are shown changed. */
#define SVZ_CHANGE_RANGES     0x004u /**< Custom y ranges changed. */
#define SVZ_CHANGE_APPEARANCE 0x008u /**< Colors or axis sides changed. */
#define SVZ_CHANGE_TITLE      0x010u /**< Title text or visibility changed. */
#define SVZ_CHANGE_UTC_OFFSET 0x020u /**< UTC offset changed. */
#define SVZ_CHANGE_CURSORS    0x040u /**< Cursor visibility changed. */
#define SVZ_CHANGE_GAPS       0x080u /**< Gap mode changed. */
#define SVZ_CHANGE_PARAMETERS 0x100u /**< A transform parameter changed. */
/** @} */

/**
 * @def     SVZ_GAP_MARKER
 * @brief   The index svz_envelope() writes for a gap marker: a point that
 *          breaks the line, with a NaN value.
 */
#define SVZ_GAP_MARKER UINT64_MAX

/** Opaque handle to one opened log, from svz_session_new() until svz_session_free(). */
typedef struct svz_session svz_session;

/**
 * Opaque handle to the per-tag display preferences, from
 * svz_preferences_new() until svz_preferences_free(). It outlives sessions:
 * preferences for several tags are kept while logs come and go.
 */
typedef struct svz_preferences svz_preferences;

/** @return SVZ_ABI_VERSION as the library was built. */
TLW_API uint32_t svz_abi_version(void);

/* -- Preferences ---------------------------------------------------------- */

/** @return A new, empty preference store; NULL only when memory runs out. */
TLW_API svz_preferences *svz_preferences_new(void);

/** Frees @p p; NULL is ignored. */
TLW_API void svz_preferences_free(svz_preferences *p);

/**
 * @brief   Replaces every entry with those of a preference file.
 * @param[in] p       The store.
 * @param[in] json    The file's contents, UTF-8, @p len bytes, not
 *                    necessarily NUL-terminated.
 * @param[in] len     Length of @p json in bytes.
 * @param[in] source  How to name the file in messages, normally its path.
 * @return  1 on success; 0, leaving the store unchanged, when the file is
 *          refused, with svz_preferences_last_error() saying why.
 */
TLW_API int svz_preferences_load(svz_preferences *p, const char *json, size_t len,
                                 const char *source);

/**
 * @brief   The preference file for every entry, byte for byte as the Qt
 *          sensorViz writes it.
 * @return  UTF-8 text owned by @p p, valid until the next call on it.
 */
TLW_API const char *svz_preferences_save(svz_preferences *p);

/** @return Why the last svz_preferences_load() failed; "" if it did not. */
TLW_API const char *svz_preferences_last_error(const svz_preferences *p);

/* -- Sessions -------------------------------------------------------------- */

/** @return A new session with no log; NULL only when memory runs out. */
TLW_API svz_session *svz_session_new(void);

/** Frees @p s; NULL is ignored. */
TLW_API void svz_session_free(svz_session *s);

/** @return Why the last failing call on @p s failed; "" if none has. */
TLW_API const char *svz_last_error(const svz_session *s);

/**
 * @brief   Opens a log and does everything sensorViz's File > Load does.
 *
 * @details Reads the log, resets the view for its streams with the file name
 *          as title, runs the display transforms, and applies @p p's entry for
 *          the tag. Session settings (UTC offset, parameters) carry over from
 *          the previous log.
 *
 * @param[in] s     The session.
 * @param[in] path  The log, UTF-8.
 * @param[in] p     Preferences to apply; may be NULL.
 * @return  1 on success; 0, leaving the session as it was, when the log
 *          cannot be read, with svz_last_error() saying why. Transforms that
 *          fail do not fail the open; see svz_catalog_json()'s
 *          `transformFailures`.
 */
TLW_API int svz_open(svz_session *s, const char *path, const svz_preferences *p);

/**
 * @brief   Applies @p p's entry for the open log's tag, or the defaults when
 *          it has none (Preferences > Load, Load Defaults).
 * @return  SVZ_CHANGE_* bits.
 */
TLW_API uint32_t svz_apply_preferences(svz_session *s, const svz_preferences *p);

/** Stores the open log's display choices in @p p under its tag (before Store, or another log). */
TLW_API void svz_remember_preferences(const svz_session *s, svz_preferences *p);

/** Removes @p p's entry for the open log's tag, so that applying @p p restores the defaults. */
TLW_API void svz_forget_preferences(const svz_session *s, svz_preferences *p);

/* -- Reading the state ------------------------------------------------------ */

/**
 * @brief   The open log: path, tag, info rows, record sets, event markers,
 *          calibration, compass source and transform failures.
 * @return  JSON object owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_catalog_json(svz_session *s);

/**
 * @brief   The view state: every stream with its metadata, effective color
 *          and side, visibility and plot-data handle; the visible ids in
 *          drawing order; custom ranges; title, UTC offset, cursors, gaps;
 *          and the parameter values.
 * @return  JSON object owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_view_json(svz_session *s);

/**
 * @brief   The PlotSpec: what to draw and how, every field.
 * @return  JSON object owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_spec_json(svz_session *s);

/**
 * @brief   The parameters of the transforms that apply to the open log, with
 *          their current values, for building their controls.
 * @return  JSON array owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_parameters_json(svz_session *s);

/**
 * @brief   The hover readout at @p time: the time text, then each visible
 *          stream's `label: value units`, in drawing order.
 * @return  JSON array of strings owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_hover_json(svz_session *s, double time);

/**
 * @brief   @p time as the readout shows it, in the log's domain and the view's UTC offset.
 * @return  Text owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_format_time(svz_session *s, double time);

/**
 * @brief   The range the range dialog starts from for stream @p id: its
 *          custom range, else its default.
 * @param[in]  s      The session.
 * @param[in]  id     Stream id.
 * @param[out] lower  Lower bound.
 * @param[out] upper  Upper bound.
 * @return  1; 0, writing nothing, when @p id is not a stream.
 */
TLW_API int svz_range_for_dialog(const svz_session *s, const char *id, double *lower, double *upper);

/**
 * @brief   One stream's M4 envelope: at most four points per column, plus gap
 *          markers, for drawing.
 *
 * @param[in]  s         The session.
 * @param[in]  stream    The plot-data handle (`stream` in the spec's traces
 *                       or the view's streams).
 * @param[in]  t0, t1    The window, seconds in the log's domain; -inf and
 *                       +inf mean the first and last sample.
 * @param[in]  columns   Columns, normally the plot's width in device pixels.
 * @param[in]  break_gaps  Nonzero to break lines at gaps (the spec's
 *                       `breakAtGaps`).
 * @param[out] time      Point times, @p capacity of them; may be NULL when
 *                       @p capacity is 0.
 * @param[out] value     Point values as stored; NaN at a gap marker.
 * @param[out] index     Sample index of each point, or SVZ_GAP_MARKER.
 * @param[in]  capacity  Room in each array.
 *
 * @return  The number of points. When it exceeds @p capacity nothing is
 *          written: call again with arrays that large. 0 for an unknown
 *          handle, a bad window, or @p columns below 1.
 */
TLW_API size_t svz_envelope(svz_session *s, uint64_t stream, double t0, double t1, int columns,
                            int break_gaps, double *time, float *value, uint64_t *index,
                            size_t capacity);

/**
 * @brief   The compass orientation samples, for a compass display.
 * @param[in]  s         The session.
 * @param[out] time      Sample times, seconds in the log's domain.
 * @param[out] values    Seven floats per sample: quaternion w, x, y, z, dip
 *                       (degrees), field (µT), acceleration (mg).
 * @param[in]  capacity  Room for samples (time holds @p capacity, values
 *                       7 * @p capacity).
 * @return  The number of samples; when it exceeds @p capacity nothing is written.
 */
TLW_API size_t svz_compass_samples(svz_session *s, double *time, float *values, size_t capacity);

/**
 * @brief   Takes the open log through the state dump's script and returns
 *          the dump (see host/applications/sensorviz/core/state_dump.h).
 * @warning Changes the session, as the script does: use a session opened for
 *          the purpose.
 * @return  Text owned by @p s, valid until the next call on it.
 */
TLW_API const char *svz_state_dump(svz_session *s);

/* -- Changing the view ---------------------------------------------------- */

/** Shows or hides one stream. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_stream_visible(svz_session *s, const char *id, int visible);

/** Shows exactly the @p count streams in @p ids. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_visible_streams(svz_session *s, const char *const *ids, size_t count);

/**
 * @brief   Sets a y range the user chose, and the ranges linked to it: its
 *          axis group, altitude from pressure, activity and its filter.
 * @return  SVZ_CHANGE_* bits.
 */
TLW_API uint32_t svz_set_stream_range(svz_session *s, const char *id, double lower, double upper);

/** Reset Zoom: clears every custom range. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_reset_zoom(svz_session *s);

/**
 * @brief   Replaces the color overrides with @p count of them.
 * @param[in] s     The session.
 * @param[in] ids   Stream ids.
 * @param[in] rgb   Colors as 0xRRGGBB.
 * @param[in] count Entries in @p ids and @p rgb.
 * @return  SVZ_CHANGE_* bits.
 */
TLW_API uint32_t svz_set_colors(svz_session *s, const char *const *ids, const uint32_t *rgb,
                                size_t count);

/**
 * @brief   Replaces the axis-side overrides with @p count of them.
 * @param[in] s      The session.
 * @param[in] ids    Stream ids.
 * @param[in] right  Nonzero for the right side, zero for the left.
 * @param[in] count  Entries in @p ids and @p right.
 * @return  SVZ_CHANGE_* bits.
 */
TLW_API uint32_t svz_set_axis_sides(svz_session *s, const char *const *ids, const int *right,
                                    size_t count);

/** Sets the title text, UTF-8. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_title(svz_session *s, const char *title);
/** Shows or hides the title. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_title_visible(svz_session *s, int visible);
/** Sets the UTC offset for epoch times, hours. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_utc_offset(svz_session *s, int hours);
/** Shows or hides the time cursors. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_cursors_visible(svz_session *s, int visible);
/** Breaks lines at gaps (nonzero) or joins them. @return SVZ_CHANGE_* bits. */
TLW_API uint32_t svz_set_break_at_gaps(svz_session *s, int break_gaps);

/**
 * @brief   Sets a transform parameter and remakes what it affects.
 * @param[in]  s        The session.
 * @param[in]  id       Parameter id, from svz_parameters_json().
 * @param[in]  value    The value; a toggle is on when nonzero.
 * @param[out] changes  SVZ_CHANGE_* bits; may be NULL.
 * @return  1; 0 when the transform failed to remake its outputs, with
 *          svz_last_error() saying why.
 */
TLW_API int svz_set_parameter(svz_session *s, const char *id, double value, uint32_t *changes);

#ifdef __cplusplus
}
#endif

#endif /* SENSORVIZ_C_H */
